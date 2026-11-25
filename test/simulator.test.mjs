import assert from 'node:assert/strict';
import { once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import { test } from 'node:test';
import { createGatewayServer } from '../apps/gateway/server.mjs';
import { startSimulator } from '../apps/simulator/probe.mjs';

test('simulator registers again after its device disappears and stops without leaving ownership', async () => {
  const server = createGatewayServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const baseUrl = 'http://127.0.0.1:' + server.address().port;
  const errors = [];
  let stop;
  try {
    stop = await startSimulator({ baseUrl, deviceId: 'reconnect', intervalMs: 10,
      timeoutMs: 1000, onError: (error) => errors.push(error) });
    await fetch(baseUrl + '/api/v1/devices/reconnect', { method: 'DELETE' });
    const deadline = Date.now() + 2000;
    let count = 0;
    while (Date.now() < deadline && count !== 1) {
      await delay(15);
      count = (await (await fetch(baseUrl + '/api/v1/devices')).json()).devices.length;
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
