import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { once } from 'node:events';
import { test } from 'node:test';
import vm from 'node:vm';
import { createGatewayServer } from '../apps/gateway/server.mjs';

const source = await readFile(new URL('../apps/console/app.js', import.meta.url), 'utf8');
class Element {
  children = [];
  listeners = new Map();
  textContent = '';
  disabled = false;
  classList = { add() {}, remove() {}, toggle() {} };
  setAttribute() {}
  replaceChildren() { this.children = []; }
  append(...items) { this.children.push(...items); }
  addEventListener(event, callback) { this.listeners.set(event, callback); }
  fire(event) { return this.listeners.get(event)?.({ preventDefault() {} }); }
}
function page(base, storage = new Map()) {
  const elements = new Map();
  const timers = new Map();
  let nextTimer = 0;
  const context = vm.createContext({
    document: {
      getElementById(id) { if (!elements.has(id)) elements.set(id, new Element()); return elements.get(id); },
      createElement() { return new Element(); },
    },
    sessionStorage: { getItem: (key) => storage.get(key) ?? null,
      setItem: (key, value) => storage.set(key, value), removeItem: (key) => storage.delete(key) },
    fetch: (path, options) => fetch(new URL(path, base), options), AbortSignal,
    crypto: { randomUUID },
    setTimeout(fn) { const id = ++nextTimer; timers.set(id, fn); return id; },
    clearTimeout(id) { timers.delete(id); },
  });
  vm.runInContext(source, context);
  return { elements, timers, storage, ready: () => vm.runInContext('refresh()', context) };
}
test('console restores its session, prevents duplicate mutations and clears deleted probes', async () => {
  const server = createGatewayServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const base = 'http://127.0.0.1:' + server.address().port;
  try {
    const first = page(base);
    await first.ready();
    await Promise.all([first.elements.get('connect-button').fire('click'), first.elements.get('connect-button').fire('click')]);
    let devices = (await (await fetch(base + '/api/v1/devices')).json()).devices;
    assert.equal(devices.length, 1);
    await Promise.all([first.elements.get('acquire-button').fire('click'), first.elements.get('acquire-button').fire('click')]);
    const id = first.storage.get('tetherbench.session');
    assert.ok(id);
    assert.equal((await (await fetch(base + '/api/v1/health')).json()).sessions, 1);
    const second = page(base, first.storage);
    await second.ready();
    assert.equal(second.elements.get('release-button').disabled, false);
    assert.match(second.elements.get('session-state').textContent, /Session active/);
    await fetch(base + '/api/v1/devices/' + devices[0].id, { method: 'DELETE' });
    await second.ready();
    assert.equal(second.storage.has('tetherbench.session'), false);
    assert.equal(second.storage.has('tetherbench.browserProbe'), false);
    assert.equal(second.elements.get('connect-button').disabled, false);
    assert.equal(second.timers.size, 0);
    await second.elements.get('connect-button').fire('click');
    devices = (await (await fetch(base + '/api/v1/devices')).json()).devices;
    assert.equal(devices.length, 1);
  } finally {
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
  }
});
