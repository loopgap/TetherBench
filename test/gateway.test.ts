import assert from 'node:assert/strict';
import { request as httpRequest } from 'node:http';
import { readFile } from 'node:fs/promises';
import { after, before, describe, it } from 'node:test';
import type { Server } from 'node:http';
import { createGatewayServer } from '../apps/gateway/server.ts';

const vectors = JSON.parse(await readFile(new URL('../contracts/test-vectors/mock-gateway.json', import.meta.url), 'utf8')) as {
  device: { id: string; name: string; capabilities: string[] }; exchange: { data: string };
};
const projectVersion = (await readFile(new URL('../VERSION', import.meta.url), 'utf8')).trim();
const packageManifest = JSON.parse(await readFile(new URL('../package.json', import.meta.url), 'utf8')) as Record<string, unknown>;
let server: Server | undefined;
let baseUrl = '';
type ApiResult = { status: number; body: any };
type ApiOptions = { method?: string; body?: unknown; headers?: Record<string, string> };

function loopbackUrl(activeServer: Server): string {
  const address = activeServer.address();
  if (!address || typeof address === 'string') throw new Error('Expected an active TCP listener.');
  return `http://127.0.0.1:${address.port}`;
}

it('uses VERSION as the only project-version source', () => {
  assert.equal(Object.hasOwn(packageManifest, 'version'), false);
});

async function start(options: Parameters<typeof createGatewayServer>[0] = {}): Promise<void> {
  const activeServer = createGatewayServer(options); server = activeServer;
  await new Promise<void>((resolve, reject) => {
    activeServer.once('error', reject);
    activeServer.listen(0, '127.0.0.1', () => resolve());
  });
  baseUrl = loopbackUrl(activeServer);
}

async function stop(): Promise<void> {
  const activeServer = server;
  if (!activeServer?.listening) return;
  await new Promise<void>((resolve, reject) => activeServer.close((error) => error ? reject(error) : resolve()));
}

async function api(path: string, { method = 'GET', body, headers = {} }: ApiOptions = {}): Promise<ApiResult> {
  const response = await fetch(new URL(path, baseUrl), {
    method,
    headers: body === undefined ? headers : { 'Content-Type': 'application/json', ...headers },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  return { status: response.status, body: response.status === 204 ? null : await response.json() as any };
}

describe('local Gateway prototype', () => {
  before(async () => start());
  after(stop);

  it('serves the console and reports that this is a prototype', async () => {
    const page = await fetch(baseUrl);
    assert.equal(page.status, 200);
    assert.match(page.headers.get('content-security-policy') ?? '', /default-src 'self'/);
    assert.match(await page.text(), /设备清单/);
    assert.deepEqual((await api('/api/v1/health')).body, {
      state: 'prototype', version: projectVersion, devices: 0, sessions: 0,
    });
  });

  it('returns the public device object after a heartbeat', async () => {
    const registered = await api('/api/v1/devices', {
      method: 'POST', body: { ...vectors.device, id: 'heartbeat-probe' },
    });
    try {
      const heartbeat = await api('/api/v1/devices/heartbeat-probe/heartbeat', { method: 'POST' });
      assert.equal(heartbeat.status, 200);
      assert.deepEqual(heartbeat.body, { ...registered.body, lastSeenAt: heartbeat.body.lastSeenAt });
    } finally {
      await api('/api/v1/devices/heartbeat-probe', { method: 'DELETE' });
    }
  });

  it('registers a device, grants one session, exchanges UTF-8 data, and releases ownership', async () => {
    const registered = await api('/api/v1/devices', { method: 'POST', body: vectors.device });
    assert.equal(registered.status, 201);
    assert.equal(registered.body.sessionActive, false);

    const listed = await api('/api/v1/devices');
    assert.equal(listed.body.devices[0].id, vectors.device.id);
    assert.equal('sessionId' in listed.body.devices[0], false);

    const acquired = await api(`/api/v1/devices/${vectors.device.id}/sessions`, { method: 'POST' });
    assert.equal(acquired.status, 201);
    assert.deepEqual(await api(`/api/v1/sessions/${acquired.body.id}`), { status: 200, body: acquired.body });
    assert.equal((await api(`/api/v1/devices/${vectors.device.id}/sessions`, { method: 'POST' })).status, 409);
    assert.equal((await api('/api/v1/devices')).body.devices[0].sessionActive, true);

    const exchanged = await api(`/api/v1/sessions/${acquired.body.id}/exchange`, {
      method: 'POST', body: vectors.exchange,
    });
    assert.equal(exchanged.status, 200);
    assert.equal(exchanged.body.data, vectors.exchange.data);
    assert.equal(exchanged.body.byteLength, Buffer.byteLength(vectors.exchange.data));

    assert.equal((await api(`/api/v1/sessions/${acquired.body.id}`, { method: 'DELETE' })).body.released, true);
    assert.equal((await api(`/api/v1/sessions/${acquired.body.id}`)).status, 404);
    assert.equal((await api(`/api/v1/devices/${vectors.device.id}/sessions`, { method: 'POST' })).status, 201);
    const disconnected = await api(`/api/v1/devices/${vectors.device.id}`, { method: 'DELETE' });
    assert.deepEqual(disconnected.body, { disconnected: true, releasedSession: true });
    assert.deepEqual((await api('/api/v1/health')).body, {
      state: 'prototype', version: projectVersion, devices: 0, sessions: 0,
    });
  });

  it('rejects an exchange whose session is released while its body is arriving', async () => {
    const registered = await api('/api/v1/devices', {
      method: 'POST', body: { ...vectors.device, id: 'revoked-during-exchange' },
    });
    const acquired = await api('/api/v1/devices/' + registered.body.id + '/sessions', { method: 'POST' });
    const path = '/api/v1/sessions/' + acquired.body.id + '/exchange';
    const requestStarted = new Promise((resolve) => {
      const observe = (request) => {
        if (request.url === path) { server.off('request', observe); resolve(); }
      };
      server.on('request', observe);
    });
    let pendingRequest;
    const responsePromise = new Promise((resolve, reject) => {
      pendingRequest = httpRequest(new URL(path, baseUrl), {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
      }, resolve);
      pendingRequest.on('error', reject);
      pendingRequest.write('{"data":"partial');
    });

    try {
      await requestStarted;
      await api('/api/v1/sessions/' + acquired.body.id, { method: 'DELETE' });
      pendingRequest.end('"}');
      const response = await responsePromise;
      const drained = new Promise((resolve, reject) => {
        response.once('end', resolve);
        response.once('error', reject);
      });
      response.resume();
      await drained;
      assert.equal(response.statusCode, 404);
    } finally {
      if (!pendingRequest.destroyed) pendingRequest.destroy();
      await api('/api/v1/devices/' + registered.body.id, { method: 'DELETE' });
    }
  });

  it('rejects invalid input, oversized bodies, and cross-origin writes', async () => {
    assert.equal((await api('/api/v1/devices', { method: 'POST', body: { ...vectors.device, id: '../escape' } })).status, 400);
    assert.equal((await api('/api/v1/devices', { method: 'POST', body: { ...vectors.device, capabilities: ['BAD'] } })).status, 400);
    assert.equal((await api('/api/v1/devices', {
      method: 'POST', body: vectors.device, headers: { Origin: 'https://attacker.invalid' },
    })).status, 403);

    const oversized = await fetch(new URL('/api/v1/devices', baseUrl), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ...vectors.device, name: 'x'.repeat(20_000) }),
    });
    assert.equal(oversized.status, 413);
  });
});

it('expires a silent probe and releases its session', async () => {
  await stop();
  await start({ heartbeatTimeoutMs: 120, sweepIntervalMs: 10 });
  try {
    await api('/api/v1/devices', { method: 'POST', body: { ...vectors.device, id: 'silent-probe' } });
    await api('/api/v1/devices/silent-probe/sessions', { method: 'POST' });
    await new Promise((resolve) => setTimeout(resolve, 180));
    assert.deepEqual((await api('/api/v1/health')).body, {
      state: 'prototype', version: projectVersion, devices: 0, sessions: 0,
    });
  } finally {
    await stop();
  }
});
