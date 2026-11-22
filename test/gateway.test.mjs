import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { after, before, describe, it } from 'node:test';
import { createGatewayServer } from '../apps/gateway/server.mjs';

const vectors = JSON.parse(await readFile(new URL('../contracts/test-vectors/mock-gateway.json', import.meta.url), 'utf8'));
const projectVersion = (await readFile(new URL('../VERSION', import.meta.url), 'utf8')).trim();
const packageManifest = JSON.parse(await readFile(new URL('../package.json', import.meta.url), 'utf8'));
let server;
let baseUrl;

it('uses VERSION as the only project-version source', () => {
  assert.equal(Object.hasOwn(packageManifest, 'version'), false);
});

async function start(options) {
  server = createGatewayServer(options);
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  baseUrl = `http://127.0.0.1:${server.address().port}`;
}

async function stop() {
  if (!server?.listening) return;
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
}

async function api(path, { method = 'GET', body, headers = {} } = {}) {
  const response = await fetch(new URL(path, baseUrl), {
    method,
    headers: body === undefined ? headers : { 'Content-Type': 'application/json', ...headers },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  return { status: response.status, body: response.status === 204 ? null : await response.json() };
}

describe('local Gateway prototype', () => {
  before(async () => start());
  after(stop);

  it('serves the console and reports that this is a prototype', async () => {
    const page = await fetch(baseUrl);
    assert.equal(page.status, 200);
    assert.match(page.headers.get('content-security-policy'), /default-src 'self'/);
    assert.match(await page.text(), /Probe inventory/);
    assert.deepEqual((await api('/api/v1/health')).body, {
      state: 'prototype', version: projectVersion, devices: 0, sessions: 0,
    });
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
    assert.equal((await api(`/api/v1/devices/${vectors.device.id}/sessions`, { method: 'POST' })).status, 409);
    assert.equal((await api('/api/v1/devices')).body.devices[0].sessionActive, true);

    const exchanged = await api(`/api/v1/sessions/${acquired.body.id}/exchange`, {
      method: 'POST', body: vectors.exchange,
    });
    assert.equal(exchanged.status, 200);
    assert.equal(exchanged.body.data, vectors.exchange.data);
    assert.equal(exchanged.body.byteLength, Buffer.byteLength(vectors.exchange.data));

    assert.equal((await api(`/api/v1/sessions/${acquired.body.id}`, { method: 'DELETE' })).body.released, true);
    assert.equal((await api(`/api/v1/devices/${vectors.device.id}/sessions`, { method: 'POST' })).status, 201);
    const disconnected = await api(`/api/v1/devices/${vectors.device.id}`, { method: 'DELETE' });
    assert.deepEqual(disconnected.body, { disconnected: true, releasedSession: true });
    assert.deepEqual((await api('/api/v1/health')).body, {
      state: 'prototype', version: projectVersion, devices: 0, sessions: 0,
    });
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
