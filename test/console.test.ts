import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { once } from 'node:events';
import type { AddressInfo } from 'node:net';
import { test } from 'node:test';
import vm from 'node:vm';
import { createGatewayServer } from '../apps/gateway/server.ts';

const source = await readFile(new URL('../build/console/app.js', import.meta.url), 'utf8');
type MockEvent = { preventDefault(): void };
type Listener = (event: MockEvent) => unknown;
class Element {
  children: Element[] = [];
  listeners = new Map<string, Listener>();
  textContent = '';
  disabled = false;
  classList = { add() {}, remove() {}, toggle() {} };
  setAttribute(_name: string, _value: string): void {}
  replaceChildren(): void { this.children = []; }
  append(...items: Element[]): void { this.children.push(...items); }
  addEventListener(event: string, callback: Listener): void { this.listeners.set(event, callback); }
  fire(event: string): unknown { return this.listeners.get(event)?.({ preventDefault() {} }); }
}
function page(base: string, storage = new Map<string, string>()) {
  const elements = new Map<string, Element>();
  const timers = new Map<number, () => unknown>();
  let nextTimer = 0;
  const context = vm.createContext({
    document: {
      getElementById(id: string) { if (!elements.has(id)) elements.set(id, new Element()); return elements.get(id)!; },
      createElement() { return new Element(); },
    },
    sessionStorage: { getItem: (key: string) => storage.get(key) ?? null,
      setItem: (key: string, value: string) => storage.set(key, value), removeItem: (key: string) => storage.delete(key) },
    fetch: (path: string, options?: RequestInit) => fetch(new URL(path, base), options), AbortSignal,
    crypto: { randomUUID },
    setTimeout(fn: () => unknown) { const id = ++nextTimer; timers.set(id, fn); return id; },
    clearTimeout(id: number) { timers.delete(id); },
  });
  vm.runInContext(source, context);
  return { elements, element: (id: string) => elements.get(id)!, timers, storage, ready: () => vm.runInContext('refresh()', context) };
}
test('console restores its session, prevents duplicate mutations and clears deleted probes', async () => {
  const server = createGatewayServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const address = server.address();
  assert.ok(address && typeof address !== 'string');
  const base = 'http://127.0.0.1:' + (address as AddressInfo).port;
  try {
    const first = page(base);
    await first.ready();
    await Promise.all([first.element('connect-button').fire('click'), first.element('connect-button').fire('click')]);
    let devices = (await (await fetch(base + '/api/v1/devices')).json()).devices;
    assert.equal(devices.length, 1);
    await Promise.all([first.element('acquire-button').fire('click'), first.element('acquire-button').fire('click')]);
    const id = first.storage.get('tetherbench.session');
    assert.ok(id);
    assert.equal((await (await fetch(base + '/api/v1/health')).json()).sessions, 1);
    const second = page(base, first.storage);
    await second.ready();
    assert.equal(second.element('release-button').disabled, false);
    assert.match(second.element('session-state').textContent, /会话进行中/);
    await fetch(base + '/api/v1/devices/' + devices[0].id, { method: 'DELETE' });
    await second.ready();
    assert.equal(second.storage.has('tetherbench.session'), false);
    assert.equal(second.storage.has('tetherbench.browserProbe'), false);
    assert.equal(second.element('connect-button').disabled, false);
    assert.equal(second.timers.size, 0);
    await second.element('connect-button').fire('click');
    devices = (await (await fetch(base + '/api/v1/devices')).json()).devices;
    assert.equal(devices.length, 1);
  } finally {
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
  }
});
