import { randomUUID } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { fileURLToPath } from 'node:url';

const MAX_BODY_BYTES = 16 * 1024;
const MAX_DEVICES = 64;
const MAX_CAPABILITIES = 16;
const MAX_DATA_BYTES = 4096;
const PROJECT_VERSION = (await readFile(new URL('../../VERSION', import.meta.url), 'utf8')).trim();
const DEVICE_ID = /^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$/;
const CAPABILITY = /^[a-z][a-z0-9._-]{0,63}$/;
const STATIC_FILES = new Map([
  ['/', ['../console/index.html', 'text/html; charset=utf-8']],
  ['/app.js', ['../console/app.js', 'text/javascript; charset=utf-8']],
  ['/styles.css', ['../console/styles.css', 'text/css; charset=utf-8']],
]);

class HttpError extends Error {
  constructor(status, code, message) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

function json(response, status, value) {
  response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' });
  response.end(JSON.stringify(value));
}

function fail(status, code, message) {
  throw new HttpError(status, code, message);
}

async function readJson(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > MAX_BODY_BYTES) fail(413, 'body_too_large', 'Request body exceeds 16 KiB.');
    chunks.push(chunk);
  }
  if (request.headers['content-type']?.split(';', 1)[0].trim().toLowerCase() !== 'application/json') {
    fail(415, 'unsupported_media_type', 'Content-Type must be application/json.');
  }
  if (size === 0) fail(400, 'empty_body', 'A JSON request body is required.');
  try {
    return JSON.parse(Buffer.concat(chunks).toString('utf8'));
  } catch {
    fail(400, 'invalid_json', 'Request body is not valid JSON.');
  }
}

function validateDevice(value) {
  if (!value || Array.isArray(value) || typeof value !== 'object') {
    fail(400, 'invalid_device', 'Device must be a JSON object.');
  }
  if (typeof value.id !== 'string' || !DEVICE_ID.test(value.id)) {
    fail(400, 'invalid_device_id', 'Device ID has an invalid format.');
  }
  if (typeof value.name !== 'string' || value.name.trim().length < 1 || value.name.length > 64) {
    fail(400, 'invalid_device_name', 'Device name must contain 1 to 64 characters.');
  }
  if (!Array.isArray(value.capabilities) || value.capabilities.length > MAX_CAPABILITIES
    || value.capabilities.some((item) => typeof item !== 'string' || !CAPABILITY.test(item))) {
    fail(400, 'invalid_capabilities', 'Capabilities must contain at most 16 valid names.');
  }
  if (new Set(value.capabilities).size !== value.capabilities.length) {
    fail(400, 'duplicate_capability', 'Capabilities must be unique.');
  }
}

function publicDevice(device) {
  return {
    id: device.id,
    name: device.name,
    capabilities: device.capabilities,
    connected: true,
    lastSeenAt: device.lastSeenAt,
    sessionActive: Boolean(device.sessionId),
  };
}

export function createGatewayServer({ heartbeatTimeoutMs = 15_000, sweepIntervalMs = 1_000 } = {}) {
  const devices = new Map();
  const sessions = new Map();

  function removeDevice(id) {
    const device = devices.get(id);
    if (!device) return false;
    if (device.sessionId) sessions.delete(device.sessionId);
    devices.delete(id);
    return true;
  }

  async function handle(request, response) {
    const port = server.address()?.port;
    const allowedHosts = new Set([`127.0.0.1:${port}`, `localhost:${port}`]);
    if (!allowedHosts.has(request.headers.host)) {
      json(response, 421, { error: { code: 'invalid_host', message: 'Use the loopback Gateway address.' } });
      return;
    }
    if (request.headers.origin) {
      const allowedOrigins = new Set([`http://127.0.0.1:${port}`, `http://localhost:${port}`]);
      if (!allowedOrigins.has(request.headers.origin)) {
        json(response, 403, { error: { code: 'origin_not_allowed', message: 'Cross-origin requests are not allowed.' } });
        return;
      }
    }

    let url;
    try {
      url = new URL(request.url, `http://127.0.0.1:${port}`);
    } catch {
      json(response, 400, { error: { code: 'invalid_url', message: 'Request URL is invalid.' } });
      return;
    }
    const pathname = url.pathname;

    if (request.method === 'GET' && STATIC_FILES.has(pathname)) {
      const [relativePath, contentType] = STATIC_FILES.get(pathname);
      const file = new URL(relativePath, import.meta.url);
      response.writeHead(200, { 'Content-Type': contentType, 'Cache-Control': 'no-store' });
      response.end(await readFile(file));
      return;
    }
    if (request.method === 'GET' && pathname === '/api/v1/health') {
      json(response, 200, { state: 'prototype', version: PROJECT_VERSION, devices: devices.size, sessions: sessions.size });
      return;
    }
    if (request.method === 'GET' && pathname === '/api/v1/devices') {
      json(response, 200, { devices: [...devices.values()].map(publicDevice) });
      return;
    }
    if (request.method === 'POST' && pathname === '/api/v1/devices') {
      const body = await readJson(request);
      validateDevice(body);
      if (devices.has(body.id)) fail(409, 'device_exists', 'A device with this ID is already registered.');
      if (devices.size >= MAX_DEVICES) fail(429, 'device_limit', 'The prototype supports at most 64 devices.');
      const now = new Date().toISOString();
      const device = {
        id: body.id,
        name: body.name.trim(),
        capabilities: body.capabilities,
        connected: true,
        lastSeenAt: now,
        sessionId: null,
      };
      devices.set(device.id, device);
      json(response, 201, publicDevice(device));
      return;
    }

    let match = pathname.match(/^\/api\/v1\/devices\/([^/]+)\/heartbeat$/);
    if (request.method === 'POST' && match) {
      const id = decodeURIComponent(match[1]);
      const device = devices.get(id);
      if (!device) fail(404, 'device_not_found', 'Device is not registered.');
      device.lastSeenAt = new Date().toISOString();
      json(response, 200, publicDevice(device));
      return;
    }

    match = pathname.match(/^\/api\/v1\/devices\/([^/]+)$/);
    if (request.method === 'DELETE' && match) {
      const id = decodeURIComponent(match[1]);
      const device = devices.get(id);
      if (!device) fail(404, 'device_not_found', 'Device is not registered.');
      const releasedSession = Boolean(device.sessionId);
      removeDevice(id);
      json(response, 200, { disconnected: true, releasedSession });
      return;
    }

    match = pathname.match(/^\/api\/v1\/devices\/([^/]+)\/sessions$/);
    if (request.method === 'POST' && match) {
      const id = decodeURIComponent(match[1]);
      const device = devices.get(id);
      if (!device) fail(404, 'device_not_found', 'Device is not registered.');
      if (device.sessionId) fail(409, 'session_busy', 'Device already has an active session.');
      const session = { id: randomUUID(), deviceId: id, createdAt: new Date().toISOString() };
      device.sessionId = session.id;
      sessions.set(session.id, session);
      json(response, 201, session);
      return;
    }

    match = pathname.match(/^\/api\/v1\/sessions\/([^/]+)\/exchange$/);
    if (request.method === 'POST' && match) {
      const id = decodeURIComponent(match[1]);
      const session = sessions.get(id);
      if (!session) fail(404, 'session_not_found', 'Session is not active.');
      const body = await readJson(request);
      if (!body || Array.isArray(body) || typeof body !== 'object' || typeof body.data !== 'string') {
        fail(400, 'invalid_data', 'Data must be a string.');
      }
      const byteLength = Buffer.byteLength(body.data, 'utf8');
      if (byteLength > MAX_DATA_BYTES) fail(413, 'data_too_large', 'Simulated data exceeds 4096 bytes.');
      json(response, 200, { data: body.data, byteLength });
      return;
    }

    match = pathname.match(/^\/api\/v1\/sessions\/([^/]+)$/);
    if (request.method === 'GET' && match) {
      const session = sessions.get(decodeURIComponent(match[1]));
      if (!session) fail(404, 'session_not_found', 'Session is not active.');
      json(response, 200, session);
      return;
    }
    if (request.method === 'DELETE' && match) {
      const id = decodeURIComponent(match[1]);
      const session = sessions.get(id);
      if (!session) fail(404, 'session_not_found', 'Session is not active.');
      sessions.delete(id);
      const device = devices.get(session.deviceId);
      if (device?.sessionId === id) device.sessionId = null;
      json(response, 200, { released: true, id });
      return;
    }

    json(response, 404, { error: { code: 'not_found', message: 'Endpoint was not found.' } });
  }

  const server = createServer((request, response) => {
    response.setHeader('X-Content-Type-Options', 'nosniff');
    response.setHeader('Referrer-Policy', 'no-referrer');
    response.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
    Promise.resolve(handle(request, response)).catch((error) => {
      if (response.headersSent || response.destroyed) return;
      if (error instanceof HttpError) {
        json(response, error.status, { error: { code: error.code, message: error.message } });
        return;
      }
      if (error instanceof URIError) {
        json(response, 400, { error: { code: 'invalid_path', message: 'Path encoding is invalid.' } });
        return;
      }
      console.error(error);
      json(response, 500, { error: { code: 'internal_error', message: 'Gateway request failed.' } });
    });
  });

  const timer = setInterval(() => {
    const cutoff = Date.now() - heartbeatTimeoutMs;
    for (const [id, device] of devices) {
      if (Date.parse(device.lastSeenAt) < cutoff) removeDevice(id);
    }
  }, Math.max(10, Math.min(sweepIntervalMs, heartbeatTimeoutMs)));
  timer.unref();
  server.once('close', () => clearInterval(timer));
  return server;
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const port = Number(process.env.TETHERBENCH_PORT ?? 4173);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    console.error('TETHERBENCH_PORT must be an integer between 1 and 65535.');
    process.exitCode = 2;
  } else {
    const server = createGatewayServer();
    server.listen(port, '127.0.0.1', () => {
      console.log(`TetherBench prototype: http://127.0.0.1:${port}`);
    });
  }
}
