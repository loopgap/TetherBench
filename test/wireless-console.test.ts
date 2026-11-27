import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import vm from 'node:vm';
import * as model from '../apps/console/control-model.ts';

const source = (await readFile(new URL('../build/console/wireless.js', import.meta.url), 'utf8')).replace(/^import[^\n]+\n/, '');

async function page(savedSessions: string | null = null, options: { fetch?: (url: string, init?: RequestInit) => Promise<unknown>; XMLHttpRequest?: new () => unknown } = {}) {
  const elements = new Map<string, any>(), state: { reduced: boolean; animations: any[]; cancelledAnimations: number } = { reduced: false, animations: [], cancelledAnimations: 0 };
  const storage = new Map<string, string>(savedSessions ? [['tetherbench.c5.sessions', savedSessions]] : []);
  const document: any = { activeElement: null, title: '',
    getElementById(id: string) { if (!elements.has(id)) elements.set(id, new Element('DIV', { id })); return elements.get(id); },
    querySelectorAll() { return navigation; },
    querySelector() { return new Element('A'); },
  };
  class Element {
    [key: string]: any;
    constructor(tagName: string, attributes: Record<string, string> = {}) { this.tagName = tagName; this.attributes = attributes; this.name = attributes.name; this.type = attributes.type; this.value = ''; this.files = []; this.dataset = {}; }
    getAttribute(name: string): string | null { return this.attributes[name] ?? null; }
    setAttribute(name: string, value: string): void { this.attributes[name] = value; }
    removeAttribute(name: string): void { delete this.attributes[name]; }
    addEventListener(_event: string, _callback: (event: any) => unknown): void {}
    matches(selector: string): boolean { return selector === ':disabled' && this.disabled; }
    focus() { document.activeElement = this; }
    setSelectionRange(start: number, end: number): void { this.selectionStart = start; this.selectionEnd = end; }
  }
  const navigation: any[] = ['devices', 'sessions', 'activity', 'settings', 'simulation'].map((name: string) => Object.assign(new Element('A'), { hash: '#/' + name }));
  const root = document.getElementById('c5-root');
  root.fields = [];
  root.contains = (field: any) => root.fields.includes(field);
  root.querySelectorAll = (selector: string) => selector.startsWith('details') ? [] : selector.startsWith('input[name]') ? root.fields.filter((field: any) => field.name) : root.fields;
  const animate = (target: string) => (frames: any[], options: any) => { const animation = { target, frames, options, cancel() { state.cancelledAnimations++; } }; state.animations.push(animation); return animation; };
  root.animate = animate('c5-root');
  document.getElementById('wireless-view').animate = animate('wireless-view');
  document.getElementById('prototype-view').animate = animate('prototype-view');
  let html = '';
  Object.defineProperty(root, 'innerHTML', { get: () => html, set(value: string) {
    html = value;
    // Replacing a subtree creates fresh controls and discards their transient state.
    root.fields = root.fields.filter((field: any) => field.name).map((field: any) => new Element(field.tagName, field.attributes));
    const title = new Element('H2', { id: 'c5-page-title', tabindex: '-1' });
    root.fields.push(title); elements.set('c5-page-title', title); document.activeElement = null;
  } });
  const context: any = vm.createContext({ ...model, document, location: { hash: '#/devices' },
    sessionStorage: { getItem(key: string) { return storage.get(key) ?? null; }, setItem(key: string, value: string) { storage.set(key, value); }, removeItem(key: string) { storage.delete(key); } },
    fetch: options.fetch ?? (async (url: string) => {
      if (savedSessions && url === '/api/v1/c5') {
        const id = Object.keys(JSON.parse(savedSessions))[0];
        return { ok: true, json: async () => ({ devices: [{ id, name: id, host: null, connected: false, info: null, sessionActive: true, lastSeenAt: null, error: null, ota: null, adapter: null }], discoveryError: null }) };
      }
      if (savedSessions && url.includes('/session/')) throw new TypeError('temporary network failure');
      return { ok: true, json: async () => ({ devices: [] }) };
    }),
    AbortSignal, Blob, setTimeout() {}, addEventListener() {}, scrollTo() {}, matchMedia: () => ({ matches: state.reduced }),
  });
  if (options.XMLHttpRequest) context.XMLHttpRequest = options.XMLHttpRequest;
  vm.runInContext(source, context);
  await new Promise((resolve) => setImmediate(() => setImmediate(resolve)));
  return { context, root, document, state, navigation, Element, storage, run: (code: string) => vm.runInContext(code, context) };
}

test('wireless console retains drafts, selected files and text focus after a failed operation', async () => {
  const view = await page();
  const text = new view.Element('TEXTAREA', { name: 'dap-data' });
  text.value = '00 f0'; text.setSelectionRange(1, 4);
  const file = new view.Element('INPUT', { name: 'image', type: 'file' });
  const files = [{ name: 'test.bin', size: 1024 }]; file.files = files;
  view.root.fields = [text, file]; text.focus();
  await view.run('mutate(async () => { throw new Error("Connection interrupted"); })');
  const restored = view.root.fields.find((field: any) => field.name === 'dap-data');
  assert.equal(restored.value, '00 f0');
  assert.strictEqual(view.root.fields.find((field: any) => field.name === 'image').files, files);
  assert.strictEqual(view.document.activeElement, restored);
  assert.deepEqual([restored.selectionStart, restored.selectionEnd], [1, 4]);
  assert.equal(view.root.getAttribute('aria-busy'), 'false');
  assert.equal(view.document.getElementById('c5-notice').textContent, 'Connection interrupted');
});

test('navigation updates its title and focus, and respects reduced motion', async () => {
  const view = await page();
  view.state.reduced = true; const before = view.state.animations.length;
  view.context.location.hash = '#/settings';
  await view.run('navigate()');
  assert.equal(view.document.title, '配置与能力 · TetherBench');
  assert.strictEqual(view.document.activeElement, view.document.getElementById('c5-page-title'));
  assert.equal(view.navigation[3].getAttribute('aria-current'), 'page');
  assert.equal(view.navigation[0].getAttribute('aria-current'), null);
  assert.equal(view.state.animations.length, before);
  assert.equal(view.state.cancelledAnimations, 1);
  view.state.reduced = false; view.context.location.hash = '#/activity';
  await view.run('navigate()');
  assert.equal(view.state.animations.length, before + 1);
  assert.equal(view.state.animations[0].options.duration, 240);
  assert.equal(view.state.animations[0].target, 'wireless-view');
  assert.match(view.state.animations[0].frames[0].transform, /scale\(\.995\)/);
  view.context.location.hash = '#/sessions';
  await view.run('navigate()');
  assert.equal(view.state.cancelledAnimations, 2);
  view.context.location.hash = '#/simulation';
  await view.run('navigate()');
  assert.equal(view.state.animations.at(-1).target, 'prototype-view');
  assert.equal(view.state.cancelledAnimations, 3);
});

test('console shell removes repeated environment copy and uses a distinct connection mark', async () => {
  const html = await readFile(new URL('../apps/console/index.html', import.meta.url), 'utf8');
  assert.doesNotMatch(html, /设备凭据保留在本机|总线事务由 C5 执行|Local first\. Built for your bench\./);
  assert.match(html, /<html lang="zh-CN">/);
  assert.match(html, /id="c5-notice" role="status" aria-live="polite" aria-atomic="true"/);
  assert.match(html, /原型最多回显 4096 字节 UTF-8 数据/);
  assert.match(html, /Gateway 仅监听回环地址/);
  assert.doesNotMatch(html, /Embedded workspace|Add browser test probe|Simulation only|This unauthenticated prototype/);
  assert.doesNotMatch(source, /凭据留在本机|证书和凭据保留在本机|设备凭据保留在本机|总线事务由 C5 执行/);
  assert.match(html, /<symbol id="icon-brand"/);
  assert.match(html, /<use href="#icon-brand"><\/use>/);
});

test('console stylesheet keeps touch targets, visible focus and reduced motion support', async () => {
  const css = await readFile(new URL('../apps/console/styles.css', import.meta.url), 'utf8');
  assert.match(css, /:focus-visible\s*\{[^}]*outline/);
  assert.match(css, /\.workspace-tabs a\s*\{[^}]*min-height:\s*44px/);
  assert.match(css, /\.sidebar a\s*\{[^}]*min-height:\s*54px/);
  assert.match(css, /@media \(prefers-reduced-motion:\s*reduce\)/);
  assert.match(css, /--accent-gradient:\s*linear-gradient\([^;]*#[0-9a-f]{6}[^;]*#[0-9a-f]{6}/i);
  assert.doesNotMatch(css, /--green\s*:/i);
});

test('device inventory escapes device text and does not turn an offline profile into an online device', async () => {
  const view = await page();
  view.context.profiles = [{ id: 'c5-test', name: '<script>test</script>', host: null, connected: false, error: '<b>no link</b>' }];
  view.run('devices = profiles; render()');
  assert.match(view.root.innerHTML, /&lt;script&gt;test&lt;\/script&gt;/);
  assert.match(view.root.innerHTML, /&lt;b&gt;no link&lt;\/b&gt;/);
  assert.doesNotMatch(view.root.innerHTML, /<script>|badge online/);
  assert.match(view.root.innerHTML, /已配对 · 等待上线/);
  assert.equal(view.document.getElementById('nav-device-count').textContent, '1');
});

test('wireless scan view decodes the versioned 16-byte sample with age', async () => {
  const view = await page(), response = Buffer.alloc(20); response.writeUInt32LE(1); Buffer.from([1, 2, 3, 4, 5, 6]).copy(response, 4);
  response[10] = 40; response.writeInt8(-58, 11); response[12] = 1; response[13] = 3; response[14] = 5; response.writeUInt32LE(750, 16);
  assert.deepEqual(JSON.parse(JSON.stringify(view.run('decodeScan("' + response.toString('hex') + '")'))),
    [{ bssid: '01:02:03:04:05:06', channel: 40, rssi: -58, wifi6: true, sightings: 5, ageMs: 750 }]);
  assert.throws(() => view.run('decodeScan("0100000001020304050628c601030500000000")'), /扫描响应长度无效/);
});

test('saved C5 session survives a temporary validation failure for a later retry', async () => {
  const saved = JSON.stringify({ 'c5-probe': { id: 'session-1', deviceId: 'c5-probe', deadline: 4102444800000 } });
  const view = await page(saved);
  assert.equal(view.run('sessions["c5-probe"].id'), 'session-1');
  assert.deepEqual(JSON.parse(view.storage.get('tetherbench.c5.sessions')!), JSON.parse(saved));
  assert.match(view.document.getElementById('c5-notice').textContent, /暂时无法验证/);
  assert.match(view.document.getElementById('c5-notice').textContent, /已保留标识/);
});

test('Gateway timeout on a mutation reports an unknown result and is not automatically replayed', async () => {
  let requests = 0;
  let timeoutError: Error = new Error('initial');
  const view = await page(null, { fetch: async () => { requests++; throw timeoutError; } });
  requests = 0;
  timeoutError = view.run('Object.assign(new Error("deadline"), { name: "TimeoutError" })');
  const error = await view.run('api("/api/v1/c5/probe/dap", "POST", { data: "00" })').then(() => null, (value: Error & { code: string }) => value);
  assert.equal(error.code, 'gateway_timeout');
  assert.match(error.message, /操作结果可能未知/);
  assert.equal(requests, 1);
});

test('invalid JSON from a mutating Gateway response preserves the unknown-result recovery guidance', async () => {
  let requests = 0;
  const view = await page(null, { fetch: async () => { requests++; return { ok: true, status: 200, json: async () => { throw new SyntaxError('invalid JSON'); } }; } });
  requests = 0;
  const error = await view.run('api("/api/v1/c5/probe/dap", "POST", { data: "00" })').then(() => null, (value: Error & { code: string }) => value);
  assert.equal(error.code, 'invalid_response');
  assert.match(error.message, /HTTP 200/);
  assert.match(error.message, /操作结果可能未知/);
  assert.equal(requests, 1);
});

test('OTA timeout tells the operator to read status and retry the same files for checkpoint resume', async () => {
  class TimeoutXhr {
    upload = {};
    ontimeout?: () => void;
    open() {}
    setRequestHeader() {}
    send() { Promise.resolve().then(() => this.ontimeout?.()); }
  }
  const view = await page(null, { XMLHttpRequest: TimeoutXhr });
  view.run('sessions["c5-probe"] = { id: "session-1", deviceId: "c5-probe", deadline: 123456 }');
  const error = await view.run('upload("c5-probe", "ota", new Blob(["manifest", "image"]))').then(() => null, (value: Error) => value);
  assert.match(error.message, /OTA 上传超时，设备接收结果未知/);
  assert.match(error.message, /先读取设备 OTA 状态/);
  assert.match(error.message, /重新选择相同 manifest 和镜像/);
  assert.match(view.document.getElementById('upload-state').textContent, /相同 manifest 和镜像/);
});

test('target OTA timeout is reported in its own recovery panel', async () => {
  const requests: TimeoutXhr[] = [];
  class TimeoutXhr {
    upload = {};
    headers: Record<string, string> = {};
    url = '';
    ontimeout?: () => void;
    constructor() { requests.push(this); }
    open(_method: string, url: string) { this.url = url; }
    setRequestHeader(name: string, value: string) { this.headers[name] = value; }
    send() { Promise.resolve().then(() => this.ontimeout?.()); }
  }
  const view = await page(null, { XMLHttpRequest: TimeoutXhr });
  view.run('sessions["c5-probe"] = { id: "session-1", deviceId: "c5-probe", deadline: 123456 }');
  const error = await view.run('upload("c5-probe", "target-ota", new Blob(["package"]), 3)').then(() => null, (value: Error) => value);
  assert.match(error.message, /OTA 上传超时，设备接收结果未知/);
  assert.match(error.message, /先读取目标 OTA 状态/);
  assert.equal(view.document.getElementById('target-upload-state').textContent, error.message);
  assert.equal(view.document.getElementById('upload-state').textContent, undefined);
  assert.equal(requests[0].url, '/api/v1/c5/c5-probe/target-ota');
  assert.equal(requests[0].headers['X-TetherBench-Protocol'], 'spi');
});

test('acknowledged OTA state replaces stale device and detail state before status refresh', async () => {
  const view = await page();
  const old = { jobId: '1', phase: 1, received: 16384, checkpoint: 16384, total: 32768, pendingBoot: false };
  const verified = { jobId: '2', phase: 2, received: 32768, checkpoint: 32768, total: 32768, pendingBoot: false };
  view.run('devices = [{ id: "c5", ota: ' + JSON.stringify(old) + ' }]; details.set("c5", { info: {}, ota: ' + JSON.stringify(old) + ', pins: [] }); cacheOta("c5", ' + JSON.stringify(verified) + ')');
  assert.deepEqual(JSON.parse(JSON.stringify(view.run('devices[0].ota'))), verified);
  assert.deepEqual(JSON.parse(JSON.stringify(view.run('details.get("c5").ota'))), verified);
});
