interface Probe {
  id: string;
  name: string;
  capabilities: string[];
  sessionActive: boolean;
}
interface Session { id: string; deviceId: string }
interface Health { version: string }
interface Exchange { byteLength: number; data: string }
interface ApiErrorBody { error?: { message?: string } }
interface ApiRequestError extends Error { status: number }
interface RequestOptions { method?: string; body?: unknown }
type ElementId = 'service-dot' | 'service-label' | 'project-version' | 'refresh-button' | 'connect-button'
  | 'device-count' | 'device-list' | 'selected-name' | 'selected-id' | 'session-state' | 'acquire-button'
  | 'release-button' | 'disconnect-button' | 'exchange-form' | 'exchange-data' | 'exchange-button'
  | 'exchange-result' | 'announcer';

const api = '/api/v1';
const elementIds: ElementId[] = [
  'service-dot', 'service-label', 'project-version', 'refresh-button', 'connect-button', 'device-count', 'device-list',
  'selected-name', 'selected-id', 'session-state', 'acquire-button', 'release-button', 'disconnect-button',
  'exchange-form', 'exchange-data', 'exchange-button', 'exchange-result', 'announcer',
];
const elements = Object.fromEntries(elementIds.map((id) => {
  const element = document.getElementById(id);
  if (!element) throw new Error('Console element #' + id + ' is missing.');
  return [id, element];
})) as Record<ElementId, HTMLElement>;
const exchangeInput = elements['exchange-data'] as HTMLInputElement;
let devices: Probe[] = [];
let selectedDeviceId: string | null = null;
let session: Session | null = null;
let browserProbeId: string | null = null;
let savedSessionId: string | null = null;
let heartbeatTimer: ReturnType<typeof setTimeout> | null = null;
let refreshPromise: Promise<void> | null = null;
let busy = false;

function announce(message: string): void { elements.announcer.textContent = message; }
function errorMessage(error: unknown): string { return error instanceof Error ? error.message : String(error); }
function errorStatus(error: unknown): number | undefined {
  return typeof error === 'object' && error !== null && 'status' in error && typeof error.status === 'number' ? error.status : undefined;
}
try {
  savedSessionId = sessionStorage.getItem('tetherbench.session');
  browserProbeId = sessionStorage.getItem('tetherbench.browserProbe');
} catch {
  announce('浏览器存储不可用，刷新页面后无法恢复会话。');
}
function remember(key: string, value: string | null): void {
  try {
    if (value) sessionStorage.setItem(key, value);
    else sessionStorage.removeItem(key);
  } catch {
    announce('浏览器存储不可用，刷新页面后无法恢复会话。');
  }
}
function setSession(value: Session | null): void {
  session = value;
  savedSessionId = value?.id ?? null;
  remember('tetherbench.session', savedSessionId);
}
function clearBrowserProbe(): void {
  browserProbeId = null;
  remember('tetherbench.browserProbe', null);
  if (heartbeatTimer) clearTimeout(heartbeatTimer);
  heartbeatTimer = null;
}
async function request<T>(path: string, { method = 'GET', body }: RequestOptions = {}): Promise<T> {
  const response = await fetch(api + path, {
    method,
    headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(5000),
  });
  const result = (response.status === 204 ? null : await response.json()) as (T & ApiErrorBody) | null;
  if (!response.ok) {
    const error = new Error(result?.error?.message ?? '请求失败（' + response.status + '）。') as ApiRequestError;
    error.status = response.status;
    throw error;
  }
  return result as T;
}
function heartbeat(): void {
  if (!browserProbeId || heartbeatTimer) return;
  heartbeatTimer = setTimeout(async () => {
    const id = browserProbeId;
    if (!id) return;
    try {
      await request<void>('/devices/' + encodeURIComponent(id) + '/heartbeat', { method: 'POST' });
    } catch (error) {
      if (errorStatus(error) === 404 && browserProbeId === id) {
        clearBrowserProbe();
        if (session?.deviceId === id) setSession(null);
        render();
      }
      announce('测试设备心跳失败：' + errorMessage(error));
    } finally {
      heartbeatTimer = null;
      heartbeat();
    }
  }, 5000);
}
function render(): void {
  elements['device-count'].textContent = devices.length + ' 台已连接';
  elements['device-list'].replaceChildren();
  if (devices.length === 0) {
    const empty = document.createElement('p');
    empty.className = 'empty-state';
    empty.textContent = '尚无已连接的探针。';
    elements['device-list'].append(empty);
  }
  for (const device of devices) {
    const row = document.createElement('button');
    row.className = 'device-row';
    row.type = 'button';
    row.disabled = busy;
    row.setAttribute('aria-pressed', String(device.id === selectedDeviceId));
    const main = document.createElement('span');
    main.className = 'device-main';
    for (const [className, value] of [
      ['device-name', device.name], ['device-id', device.id],
      ['device-capabilities', device.capabilities.join(' · ') || '设备未报告能力'],
    ]) {
      const text = document.createElement('span');
      text.className = className;
      text.textContent = value;
      main.append(text);
    }
    const status = document.createElement('span');
    status.className = 'device-status';
    status.textContent = '已连接';
    row.append(main, status);
    row.addEventListener('click', () => {
      if (busy) return;
      if (session && session.deviceId !== device.id) {
        announce('请先释放当前会话，再选择其他探针。');
        return;
      }
      selectedDeviceId = device.id;
      render();
    });
    elements['device-list'].append(row);
  }
  const selected = devices.find((device) => device.id === selectedDeviceId);
  if (!selected) selectedDeviceId = null;
  if (session && !devices.some((device) => device.id === session?.deviceId)) setSession(null);
  elements['selected-name'].textContent = selected?.name ?? '请选择设备';
  elements['selected-id'].textContent = selected?.id ?? '尚未选择设备';
  elements['session-state'].textContent = session ? '会话进行中 · ' + session.deviceId
    : selected?.sessionActive ? '设备由其他客户端占用' : '当前没有活动会话';
  elements['session-state'].classList.toggle('active', Boolean(session));
  (elements['acquire-button'] as HTMLButtonElement).disabled = busy || !selected || Boolean(session) || Boolean(selected?.sessionActive);
  (elements['release-button'] as HTMLButtonElement).disabled = busy || !session;
  (elements['disconnect-button'] as HTMLButtonElement).disabled = busy || !selected;
  (elements['exchange-button'] as HTMLButtonElement).disabled = busy || !session;
  (elements['connect-button'] as HTMLButtonElement).disabled = busy || Boolean(browserProbeId);
}
async function loadDevices(): Promise<void> {
  const health = await request<Health>('/health');
  devices = (await request<{ devices: Probe[] }>('/devices')).devices;
  if (savedSessionId) {
    try {
      setSession(await request<Session>('/sessions/' + encodeURIComponent(savedSessionId)));
      selectedDeviceId = session?.deviceId ?? null;
    } catch (error) {
      if (errorStatus(error) !== 404) throw error;
      setSession(null);
    }
  }
  if (browserProbeId && !devices.some((device) => device.id === browserProbeId)) clearBrowserProbe();
  heartbeat();
  elements['project-version'].textContent = health.version;
  elements['service-label'].textContent = 'Gateway 已就绪';
  elements['service-dot'].classList.add('online');
  render();
}
function refresh(): Promise<void> {
  refreshPromise ??= loadDevices().finally(() => { refreshPromise = null; });
  return refreshPromise;
}
function unavailable(error: unknown): void {
  elements['service-label'].textContent = 'Gateway 未连接';
  elements['service-dot'].classList.remove('online');
  announce(errorMessage(error));
}
async function mutate(action: () => Promise<void>): Promise<void> {
  if (busy) return;
  busy = true;
  render();
  try { await action(); } catch (error) { announce(errorMessage(error)); }
  finally { busy = false; render(); }
}
elements['refresh-button'].addEventListener('click', () => refresh()
  .then(() => announce('设备列表已刷新。')).catch(unavailable));
elements['connect-button'].addEventListener('click', () => mutate(async () => {
  if (browserProbeId) return;
  const device = await request<Probe>('/devices', {
    method: 'POST', body: { id: 'browser-' + crypto.randomUUID().slice(0, 8),
      name: '浏览器测试探针', capabilities: ['cmsis-dap', 'uart'] },
  });
  browserProbeId = device.id;
  remember('tetherbench.browserProbe', browserProbeId);
  selectedDeviceId = device.id;
  heartbeat();
  await refresh();
  announce('浏览器测试探针已连接。');
}));
elements['acquire-button'].addEventListener('click', () => mutate(async () => {
  if (!selectedDeviceId || session) return;
  setSession(await request<Session>('/devices/' + encodeURIComponent(selectedDeviceId) + '/sessions', { method: 'POST' }));
  elements['exchange-result'].textContent = '会话已获取；当前没有连接实际硬件。';
  await refresh();
  announce('调试会话已获取。');
}));
elements['release-button'].addEventListener('click', () => mutate(async () => {
  if (!session) return;
  try { await request<{ released: boolean }>('/sessions/' + encodeURIComponent(session.id), { method: 'DELETE' }); }
  catch (error) { if (errorStatus(error) !== 404) throw error; }
  setSession(null);
  await refresh();
  announce('调试会话已释放。');
}));
elements['disconnect-button'].addEventListener('click', () => mutate(async () => {
  if (!selectedDeviceId) return;
  const id = selectedDeviceId;
  await request<{ disconnected: boolean }>('/devices/' + encodeURIComponent(id), { method: 'DELETE' });
  if (id === browserProbeId) clearBrowserProbe();
  if (session?.deviceId === id) setSession(null);
  selectedDeviceId = null;
  elements['exchange-result'].textContent = '暂无收发数据。';
  await refresh();
  announce('探针已断开，相关会话已释放。');
}));
elements['exchange-form'].addEventListener('submit', (event) => {
  event.preventDefault();
  return mutate(async () => {
    if (!session) return;
    const result = await request<Exchange>('/sessions/' + encodeURIComponent(session.id) + '/exchange', {
      method: 'POST', body: { data: exchangeInput.value },
    });
    elements['exchange-result'].textContent = '回显（' + result.byteLength + ' 字节）：' + result.data;
    announce('模拟数据交换完成。');
  });
});
void refresh().catch(unavailable);
