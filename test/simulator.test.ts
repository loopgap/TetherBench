import assert from 'node:assert/strict';
import { once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import { test } from 'node:test';
import type { Server } from 'node:http';
import { createGatewayServer } from '../apps/gateway/server.ts';
import { startSimulator } from '../apps/simulator/probe.ts';

function loopbackUrl(server: Server): string {
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('Expected an active TCP listener.');
  return `http://127.0.0.1:${address.port}`;
}

test('simulator registers again after its device disappears and stops without leaving ownership', async () => {
  const server = createGatewayServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const baseUrl = loopbackUrl(server);
  const errors: unknown[] = [];
  let stop;
  try {
    stop = await startSimulator({ baseUrl, deviceId: 'reconnect', intervalMs: 10,
      timeoutMs: 1000, onError: (error) => errors.push(error) });
    await fetch(baseUrl + '/api/v1/devices/reconnect', { method: 'DELETE' });
    const deadline = Date.now() + 2000;
    let count = 0;
    while (Date.now() < deadline && count !== 1) {
      await delay(15);
      count = ((await (await fetch(baseUrl + '/api/v1/devices')).json()) as { devices: unknown[] }).devices.length;
    }
    assert.equal(count, 1);
    assert.deepEqual(errors, []);
    await stop(); stop = null;
    await delay(25);
    assert.equal((await (await fetch(baseUrl + '/api/v1/devices')).json()).devices.length, 0);
  } finally {
    await stop?.();
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
  }
});
