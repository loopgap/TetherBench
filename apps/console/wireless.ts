import { VIEWS, PROTOCOLS, OTA_PHASES, parseRoute, hexText, pinSelection, type DeviceView, type ProtocolId, type Route } from './control-model.ts';

interface C5Wifi { ap: boolean; ip: string | null; channel: number; rssi: number; phy: number }
interface C5Info { id: string; version: string; idf: string; allowedPins: number; freeHeap: number; minHeap: number; pendingBoot: boolean; wifi: C5Wifi | null; bandStage: number }
interface C5Ota { jobId: string; phase: 0 | 1 | 2 | 3 | 4 | 5; received: number; checkpoint: number; total: number; pendingBoot: boolean }
interface C5Session { id: string; deviceId: string; deadline: number }
interface C5PinConfig { protocol: number; count: number; hz: number; pins: number[] }
interface C5Device { id: string; name: string; host: string | null; connected: boolean; info: C5Info | null; sessionActive: boolean; lastSeenAt: string | null; error: string | null; ota: C5Ota | null; adapter: string | null }
interface C5Status { info: C5Info; ota: C5Ota; pins: C5PinConfig[] }
interface C5DeviceList { devices: C5Device[]; discoveryError: string | null }
interface C5Exchange { data: string; byteLength: number; transactionMs: number }
interface C5Adapter { configuration: string }
interface C5BandResult { channel: number; score: { current: number; candidate: number } }
interface C5ScanResult { bssid: string; channel: number; rssi: number; wifi6: boolean; sightings: number; ageMs: number }
interface GatewayError extends Error { code?: string; status?: number }
type DraftValue = string | FileList | null;
interface FocusState { tag: string; attributes: Array<[string, string]>; start: number | null; end: number | null }
interface ActivityEntry { time: string; message: string }
function requiredElement<T extends HTMLElement>(id: string): T {
  const element = document.getElementById(id);
  if (!element) throw new Error('Console element #' + id + ' is missing.');
  return element as T;
}

const root = requiredElement<HTMLElement>('c5-root'), notice = requiredElement<HTMLElement>('c5-notice');
const storageKey = 'tetherbench.c5.sessions';
const details = new Map<string, C5Status>(), drafts = new Map<string, Record<string, DraftValue>>(), outputs = new Map<string, string>();
const sessionWarnings = new Set<string>();
let devices: C5Device[] = [], sessions: Record<string, C5Session> = {}, activity: ActivityEntry[] = [], discoveryError: string | null = null, busy = false, refreshing = false;
let route: Route = parseRoute(location.hash);
let routeAnimation: Animation | null = null;
const currentDeviceId = (): string => route.page === 'device' ? route.id : '';
const currentDeviceView = (): DeviceView => route.page === 'device' ? route.view : 'overview';
try {
  const value: unknown = JSON.parse(sessionStorage.getItem(storageKey) ?? '{}');
  if (value && typeof value === 'object' && !Array.isArray(value)) {
    const stored: Record<string, C5Session> = {};
    for (const [id, candidate] of Object.entries(value as Record<string, unknown>)) {
      if (!id || !candidate || typeof candidate !== 'object') continue;
      const session = candidate as Partial<C5Session>;
      if (typeof session.id === 'string' && typeof session.deviceId === 'string' && typeof session.deadline === 'number') stored[id] = session as C5Session;
    }
    sessions = stored;
  }
} catch { /* Private browsing can disable storage. */ }
const escape = (value: unknown): string => String(value ?? '—').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c] ?? c);
const path = (id: string, suffix = '') => '/api/v1/c5/' + encodeURIComponent(id) + suffix;
const link = (id: string, view: string = 'overview') => '#/devices/' + encodeURIComponent(id) + '/' + view;
const selected = (): C5Device | undefined => { const id = currentDeviceId(); return id ? devices.find((device) => device.id === id) : undefined; };
const icon = (name: string | number): string => `<svg class="icon" aria-hidden="true"><use href="#icon-${name}"></use></svg>`;
const panel = (title: string, content: string): string => `<section class="control-panel"><h3>${title}</h3>${content}</section>`;
const button = (action: string, label: string, disabled = false, extra = ''): string => `<button type="button" class="button ${['discover', 'acquire'].includes(action) ? 'button-primary' : ['reboot', 'ota/abort'].includes(action) ? 'button-danger' : 'button-secondary'}" data-action="${action}" ${disabled ? 'disabled' : ''} ${extra}>${action === 'discover' ? icon('radar') : ''}${label}</button>`;
const facts = (entries: ReadonlyArray<readonly [string, unknown]>): string => '<dl class="facts">' + entries.map(([key, value]) => `<dt>${escape(key)}</dt><dd>${escape(value)}</dd>`).join('') + '</dl>';
function heading(title: string, description: string, actions = ''): string { return `<section class="page-heading"><div><p class="eyebrow">${route.page === 'device' ? 'DEVICE WORKSPACE' : 'YOUR CONNECTED WORKSPACE'}</p><h2 id="c5-page-title" tabindex="-1">${escape(title)}</h2><p class="subheading">${escape(description)}</p></div><div class="toolbar">${actions}</div></section>`; }
function remember() { try { sessionStorage.setItem(storageKey, JSON.stringify(sessions)); } catch { /* Sessions remain usable in this page. */ } }
function report(message: string, error = false): void {
  notice.className = 'inline-message' + (error ? ' error' : ''); notice.textContent = message;
  activity.unshift({ time: new Date().toISOString(), message }); activity.length = Math.min(activity.length, 100);
}
function isInvalidSession(error: unknown): boolean {
  if (!(error instanceof Error)) return false;
  const code = (error as GatewayError).code;
  return code === 'session_expired' || code === 'probe_4' || code === 'device_not_found';
}
function errorText(error: unknown): string { return error instanceof Error ? error.message : String(error); }
async function api<T>(url: string, method = 'GET', body?: unknown, timeout = 15000): Promise<T> {
  const mutating = ['POST', 'PUT', 'DELETE'].includes(method.toUpperCase());
  let response: Response;
  try {
    response = await fetch(url, { method, signal: AbortSignal.timeout(timeout), headers: body === undefined ? {} : { 'Content-Type': 'application/json' }, body: body === undefined ? undefined : JSON.stringify(body) });
  } catch (caught: unknown) {
    const timedOut = caught instanceof Error && caught.name === 'TimeoutError';
    const error = new Error((timedOut ? 'Gateway 请求超时。' : '无法连接本机 Gateway。')
      + (mutating ? ' 操作结果可能未知；请先读取设备状态，再决定是否手动重试。' : '')) as GatewayError;
    error.code = timedOut ? 'gateway_timeout' : 'gateway_unreachable';
    throw error;
  }
  let raw: unknown;
  try { raw = await response.json(); }
  catch {
    const error = new Error('Gateway 返回了无效的 JSON 响应（HTTP ' + response.status + '）。'
      + (mutating ? ' 操作结果可能未知；请先读取设备状态，再决定是否手动重试。' : '')) as GatewayError;
    error.code = 'invalid_response'; error.status = response.status; throw error;
  }
  const value = raw && typeof raw === 'object' ? raw as Record<string, unknown> : {};
  if (!response.ok) {
    const apiError = value.error && typeof value.error === 'object' ? value.error as Record<string, unknown> : {};
    const error = new Error(typeof apiError.message === 'string' ? apiError.message : 'Gateway 请求失败。') as GatewayError;
    if (typeof apiError.code === 'string') error.code = apiError.code;
    error.status = response.status;
    throw error;
  }
  return raw as T;
}
function sessionButtons(device: C5Device): string { return (sessions[device.id] ? button('release', '释放会话', busy, `data-id="${escape(device.id)}"`) : button('acquire', device.sessionActive ? '已被占用' : '获取控制权', busy || !device.connected || device.sessionActive, `data-id="${escape(device.id)}"`)) + button('discover', refreshing ? '正在重新连接…' : '重新发现', busy || refreshing, `data-id="${escape(device.id)}"`); }
function saveDraft(): void {
  const values: Record<string, DraftValue> = {}; for (const field of root.querySelectorAll<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>('input[name],select[name],textarea[name]')) values[field.name] = field.tagName === 'INPUT' && (field as HTMLInputElement).type === 'file' ? (field as HTMLInputElement).files : field.value;
  drafts.set(JSON.stringify(route), values);
}
function focusState(): FocusState | null {
  const field = document.activeElement;
  if (!field || !root.contains(field)) return null;
  const attributes = ['id', 'name', 'data-action', 'data-id', 'data-protocol', 'data-focus', 'href'].map((name) => [name, field.getAttribute(name)]).filter(([, value]) => value !== null);
  if (!attributes.length) return null;
  const selectable = field.tagName === 'INPUT' ? field as HTMLInputElement : field.tagName === 'TEXTAREA' ? field as HTMLTextAreaElement : null;
  return { tag: field.tagName, attributes: attributes as Array<[string, string]>, start: selectable?.selectionStart ?? null, end: selectable?.selectionEnd ?? null };
}
function restoreFocus(state: FocusState | null): void {
  if (!state) return;
  const field = [...root.querySelectorAll<HTMLElement>('a,button,input,select,textarea,summary,[tabindex]')].find((item) => item.tagName === state.tag && state.attributes.every(([name, value]) => item.getAttribute(name) === value));
  if (!field || field.matches(':disabled')) return;
  field.focus({ preventScroll: true });
  if (state.start !== null && state.end !== null && (field.tagName === 'INPUT' || field.tagName === 'TEXTAREA')) (field as HTMLInputElement | HTMLTextAreaElement).setSelectionRange(state.start, state.end);
}
function render(): void {
  const focus = focusState();
  const expanded = new Set([...root.querySelectorAll<HTMLElement>('details[open] summary[data-focus]')].map((item) => item.dataset.focus ?? ''));
  requiredElement<HTMLElement>('wireless-view').hidden = route.page === 'simulation';
  requiredElement<HTMLElement>('prototype-view').hidden = route.page !== 'simulation';
  const pageTitle = route.page === 'device' ? selected()?.name ?? '设备工作区' : ({ devices: '设备管理', sessions: '连接与会话', activity: '操作记录', settings: '配置与能力', simulation: '原型模拟器' } as Record<string, string>)[route.page] ?? '页面不存在';
  requiredElement<HTMLElement>('workspace-location').textContent = pageTitle;
  requiredElement<HTMLElement>('nav-device-count').textContent = String(devices.length);
  document.title = pageTitle + ' · TetherBench';
  root.setAttribute('aria-busy', String(busy));
  for (const anchor of document.querySelectorAll<HTMLAnchorElement>('.sidebar a')) { if (anchor.hash === '#/' + (route.page === 'device' ? 'devices' : route.page)) anchor.setAttribute('aria-current', 'page'); else anchor.removeAttribute('aria-current'); }
  if (route.page === 'simulation') return;
  if (route.page === 'devices') {
    root.innerHTML = heading('设备管理', '连接你的工作台。从引脚配置，到无线调试。', button('discover', busy || refreshing ? '正在发现…' : '发现设备', busy || refreshing))
      + `<div class="metrics" aria-label="设备统计">${[['已配对设备', devices.length, 'devices', '台'], ['当前在线', devices.filter((d) => d.connected).length, 'wifi', '台'], ['本页会话', Object.keys(sessions).length, 'link', '个']].map(([name, count, glyph, unit]) => `<div class="metric">${icon(glyph)}<span>${name}</span><strong>${count}<small>${unit}</small></strong></div>`).join('')}</div>`
      + (discoveryError ? `<p class="inline-message error">${escape(discoveryError)}</p>` : '')
      + '<div class="section-heading"><h3>你的设备</h3><span class="section-caption">每台设备，独立会话</span></div>'
      + '<div class="inventory-layout">'
      + (devices.length ? `<div class="probe-grid">${devices.map((d) => `<article class="probe-card"><header><span class="probe-glyph">${icon('chip')}</span><div class="probe-title"><h3>${escape(d.name)}</h3><p>${escape(d.info?.id ?? d.id)}</p></div><span class="badge ${d.connected ? 'online' : ''}">${d.connected ? '在线' : '离线'}</span></header><p class="probe-meta">${icon('wifi')}${escape(d.host ?? '等待设备接入 Wi-Fi 6 网络')}</p><div class="protocol-tags" aria-label="固件协议接口">${['SWD', 'UART', 'SPI', 'I2C'].map((name) => `<span>${name}</span>`).join('')}</div>${d.error ? `<details class="connection-details"><summary data-focus="error-${escape(d.id)}">查看连接详情</summary><p>${escape(d.error)}</p></details>` : ''}<footer><span>${sessions[d.id] ? '本页持有控制权' : d.sessionActive ? '会话已被占用' : d.connected ? '已连接 · 可获取控制权' : '已配对 · 等待上线'}</span><a class="button button-secondary" href="${link(d.id)}">进入设备 ${icon('arrow')}</a></footer></article>`).join('')}</div>` : panel('准备连接第一台设备', '<p>先完成 C5 本机配对，再发现设备。</p><a class="button button-secondary" href="#/settings">查看配置方式</a>'))
      + `<aside class="control-panel connection-panel" aria-label="连接指南"><h3>无线连接，有序协作</h3><p>从本机工作区，到你的 C5。</p><div class="connection-art" aria-hidden="true">${icon('wifi')}</div><ol class="connection-steps"><li><span class="step-number">01</span><div><strong>发现设备</strong><p>在同一 Wi-Fi 6 网络中寻找已配对的 C5。</p></div></li><li><span class="step-number">02</span><div><strong>验证连接</strong><p>确认设备身份并建立安全连接。</p></div></li><li><span class="step-number">03</span><div><strong>获取控制权</strong><p>独占设备会话，再开始配置和调试。</p></div></li></ol></aside></div>`;
  } else if (route.page === 'sessions') {
    root.innerHTML = heading('连接与会话', '每个 C5 独立占用总线。页面关闭后停止续约，会话在期限内释放。') + panel('设备控制权', devices.length ? devices.map((d) => `<div class="session-row"><div><a href="${link(d.id)}">${escape(d.name)}</a><p class="muted">${sessions[d.id] ? '本页持有 · ' + escape(new Date(sessions[d.id].deadline).toLocaleTimeString()) : d.sessionActive ? '其他客户端持有' : '尚未占用'}</p></div>${sessionButtons(d)}</div>`).join('') : '<p>没有已配置设备。</p>');
  } else if (route.page === 'activity') {
    root.innerHTML = heading('操作记录', '当前页面最近 100 条操作结果，关闭页面即清除。', button('clear', '清除记录')) + panel('最近操作', activity.length ? activity.map((item) => `<div class="activity-row"><span>${escape(item.message)}</span><time datetime="${escape(item.time)}">${escape(new Date(item.time).toLocaleTimeString())}</time></div>`).join('') : '<p>尚无操作。</p>');
  } else if (route.page === 'settings') {
    root.innerHTML = heading('配置与能力', '本机管理服务通过固定证书的 TLS 通道连接 C5。') + '<div class="panel-grid">'
      + panel('设备配置', '<p>默认读取 <code>firmware/probe-c5/.local/gateway.json</code>。也可通过 <code>TETHERBENCH_C5_CONFIG</code> 指定设备列表。记录包括 id、name、credentials、certificate 和可选 host；文件路径相对于配置文件。</p><p>默认列表不存在时使用本机 <code>device.json</code> 与 <code>server.crt</code>。最多 16 个设备。发现只提供地址，证书需预先配对。</p>')
      + panel('接入条件', '<p>Gateway 只监听 127.0.0.1。C5 要求实际协商 Wi-Fi 6 HE20，网卡支持 AX 并不代表 Windows 热点支持 AX。</p><p>OpenOCD 使用本机 TCP 适配器。虚拟 USB 需要操作系统 USB/IP 客户端驱动。SPI 和 I2C 使用明确的厂商事务接口。</p><p>更新需要受信任密钥签名的 manifest。上传、激活与启动确认分别执行。</p>') + '</div>';
  } else if (route.page === 'device') renderDevice();
  else root.innerHTML = heading('页面不存在', '地址无效或该功能未定义。') + '<a href="#/devices">返回设备管理</a>';
  const saved = drafts.get(JSON.stringify(route)); if (saved) for (const field of root.querySelectorAll<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>('input[name],select[name],textarea[name]')) {
    if (!Object.hasOwn(saved, field.name)) continue;
    const value = saved[field.name];
    if (field.tagName === 'INPUT' && (field as HTMLInputElement).type === 'file') { if (value && typeof value === 'object') (field as HTMLInputElement).files = value as FileList; }
    else if (typeof value === 'string') field.value = value;
  }
  for (const item of root.querySelectorAll<HTMLElement>('details summary[data-focus]')) if (expanded.has(item.dataset.focus ?? '') && item.parentElement?.tagName === 'DETAILS') (item.parentElement as HTMLDetailsElement).open = true;
  restoreFocus(focus);
}
function renderDevice(): void {
  const deviceRoute = route;
  if (deviceRoute.page !== 'device') return;
  const device = selected(); if (!device) { root.innerHTML = heading('设备未配置', '该设备不在 Gateway 的本机配置中。') + '<a href="#/devices">返回设备管理</a>'; return; }
  const detail = details.get(device.id), info = detail?.info ?? device.info, ota = detail?.ota ?? device.ota, locked = busy || !device.connected || !sessions[device.id];
  let content = '';
  if (deviceRoute.view === 'overview') {
    content = '<div class="panel-grid">' + panel('设备状态', facts([['设备标识', info?.id ?? device.id], ['链路', device.connected ? '在线' : '离线'], ['地址', device.host], ['固件版本', info?.version], ['ESP-IDF', info?.idf], ['可用堆内存', info ? info.freeHeap + ' B' : null], ['最少可用内存', info ? info.minHeap + ' B' : null], ['最近响应', device.lastSeenAt]]))
      + panel('控制与恢复', `<p>${escape(device.error ?? '获取会话后可配置引脚并执行协议事务。')}</p><div class="form-actions">${button('status', '读取状态', busy || !device.connected)}${button('reboot', '重启 C5', locked)}${button('boot-confirm', '确认本次启动', locked || !info?.pendingBoot)}</div><p>重启释放引脚与会话。目标电气连接需按板卡原理图确认。</p>`) + '</div>';
  } else if (deviceRoute.view === 'pins') {
    content = panel('协议引脚', '<p>默认无映射，协议之间不可共用 GPIO。下列上限为配置范围，实际高速信号需测量确认。</p>' + Object.entries(PROTOCOLS).map(([protocol, spec]) => {
      const current = detail?.pins?.find((item) => item.protocol === Number(protocol)) ?? { protocol: Number(protocol), count: 0, hz: 0, pins: [] };
      const options = Array.from({ length: 29 }, (_, pin) => pin).filter((pin) => (info?.allowedPins ?? 0) & (1 << pin));
      return `<form class="control-form" data-form="pins" data-protocol="${protocol}"><h4>${spec.name}</h4><fieldset ${locked ? 'disabled' : ''}><div class="form-row">${spec.roles.map((role, index) => `<label>${role}<select name="pin-${protocol}-${index}"><option value="">未配置</option>${options.map((pin) => `<option value="${pin}" ${current?.count > index && current.pins[index] === pin ? 'selected' : ''}>GPIO ${pin}</option>`).join('')}</select></label>`).join('')}<label>${protocol === '2' ? '波特率' : '时钟（Hz）'}<input name="hz-${protocol}" type="number" min="${protocol === '1' ? 1000 : 1}" max="${spec.maximum}" value="${current?.hz || spec.hz}" required></label></div><div class="form-actions"><button class="button button-primary" type="submit">应用 ${spec.name}</button>${button('disable', '释放 ' + spec.name, locked, `data-protocol="${protocol}"`)}</div></fieldset></form>`;
    }).join(''));
  } else if (deviceRoute.view === 'terminal') {
    content = panel('UART / SPI / I2C 事务', '<p>每次收发最多 1024 B，等待 1–100 ms。SPI 同时收发长度需相同；I2C 支持重复起始。</p><form class="control-form" data-form="bus"><fieldset ' + (locked ? 'disabled' : '') + '><div class="form-row"><label>协议<select name="protocol"><option value="2">UART</option><option value="3">SPI mode 0</option><option value="4">I2C</option></select></label><label>I2C 地址（十进制，8–119）<input type="number" name="address" min="0" max="127" value="80"></label><label>接收字节数<input type="number" name="receive" min="0" max="1024" value="0"></label><label>等待（ms）<input type="number" name="timeoutMs" min="1" max="100" value="50"></label></div><label>发送数据（十六进制）<textarea name="data" rows="4" maxlength="4096" placeholder="01 02 03"></textarea></label><button class="button button-primary" type="submit">执行一次事务</button></fieldset></form>') + panel('返回数据', `<pre class="code-output" role="status">${escape(outputs.get(device.id + ':bus') ?? '尚未执行事务。')}</pre>`);
  } else if (deviceRoute.view === 'debug') {
    content = '<div class="panel-grid">' + panel('调试与 USB 适配器', '<p>OpenOCD 与 pyOCD 复用 CMSIS-DAP TCP。虚拟 USB 通过本机 USB/IP 客户端挂载；需要驱动和实际枚举验证。每个设备独占一个适配器。</p><div class="control-form"><label>接入方式<select name="adapter-type"><option value="openocd">OpenOCD / pyOCD TCP</option><option value="usb-dap">CMSIS-DAP v2 虚拟 USB</option><option value="usb-uart">UART CDC ACM 虚拟串口</option><option value="usb-spi">SPI USB 厂商事务</option><option value="usb-i2c">I2C USB 厂商事务</option></select></label></div><div class="form-actions">' + button('adapter-start', '启动适配器', locked) + button('adapter-stop', '停止适配器', locked) + '</div><pre class="code-output">' + escape(outputs.get(device.id + ':adapter') ?? '尚未启动适配器。') + '</pre>')
      + panel('CMSIS-DAP 命令', '<p>每条最多 1024 B。默认 00 F0 查询能力；目标访问前需配置 SWD 引脚。</p><form class="control-form" data-form="dap"><fieldset ' + (locked ? 'disabled' : '') + '><label>CMSIS-DAP 请求<textarea name="dap-data" rows="4" maxlength="4096">00 f0</textarea></label><button class="button button-primary" type="submit">发送命令</button></fieldset></form><pre class="code-output" role="status">' + escape(outputs.get(device.id + ':dap') ?? '尚未发送命令。') + '</pre>') + '</div>';
  } else if (deviceRoute.view === 'ota') {
    content = '<div class="panel-grid">' + panel('签名镜像上传', '<p>选择 176 B manifest 与对应的 .bin。设备验证签名与完整镜像，并按设备检查点续传。上传成功后单独激活。</p><form class="control-form" data-form="ota"><fieldset ' + (locked ? 'disabled' : '') + '><label>签名 manifest<input type="file" name="manifest" required></label><label>C5 固件<input type="file" name="image" accept=".bin" required></label><button class="button button-primary" type="submit">上传并校验</button></fieldset></form><p id="upload-state" role="status">等待文件。</p><progress class="control-progress" id="upload-progress" max="100" value="0" aria-label="传输进度"></progress>')
      + panel('设备更新状态', facts([['任务', ota?.jobId], ['阶段', OTA_PHASES.at(ota?.phase ?? -1) ?? '尚未读取'], ['已接收', ota ? `${ota.received} / ${ota.total} B` : null], ['持久化检查点', ota ? ota.checkpoint + ' B' : null], ['启动待确认', ota?.pendingBoot ? '是' : '否']]) + '<div class="form-actions">' + button('status', '读取状态', busy || !device.connected) + button('ota/activate', '激活并重启', locked || ota?.phase !== 2) + button('ota/abort', '中止更新', locked || !ota?.jobId || ota.jobId === '0' || ota.phase === 3) + button('boot-confirm', '确认本次启动', locked || !info?.pendingBoot) + '</div><p>新镜像需在 45 秒内通过认证并确认启动，否则设备回滚。不要将外部 MCU 镜像发送到 C5 自身更新槽。</p>') + '</div>';
  } else if (deviceRoute.view === 'wifi') {
    const peers = devices.filter((peer) => peer.id !== device.id && peer.connected);
    content = '<div class="panel-grid">' + panel('当前无线链路', facts([['模式', info?.wifi?.ap ? 'C5 AP' : 'STA'], ['IP', info?.wifi?.ip], ['信道', info?.wifi?.channel], ['RSSI', info?.wifi ? info.wifi.rssi + ' dBm' : null], ['PHY', info?.wifi?.phy === 5 ? 'Wi-Fi 6 HE20' : info?.wifi?.phy], ['协商阶段', info?.bandStage]]) + '<p>双 C5 协商要求两端都是空闲 HE20 STA。Windows 热点控制其自身信道，不由此操作修改。</p>') + panel('双方测量与协商', '<p>设备各做五轮 SSID 扫描。Gateway 按共同信道匹配双方可见的 Wi-Fi 6 接入点（两端 BSSID 可以不同），测量当前 TLS 往返时间；只有较差一端也提升至少 10 分才准备切换。失败时两台设备都有自动与显式补偿回滚。</p>' + (peers.length ? '<form class="control-form" data-form="band"><fieldset ' + (locked ? 'disabled' : '') + '><label>协商设备<select name="peerId" required><option value="">选择已连接的另一台 C5</option>' + peers.map((peer) => '<option value="' + escape(peer.id) + '">' + escape(peer.name) + ' · ' + escape(peer.info?.id ?? peer.id) + '</option>').join('') + '</select></label><button class="button button-primary" type="submit">测量并协商切频</button></fieldset></form>' : '<p>需要另一台已配对且在线的 C5。</p>') + '<pre class="code-output" role="status">' + escape(outputs.get(device.id + ':band') ?? '尚未执行协商。') + '</pre>') + panel('扫描候选网络', '<p>只扫描设备配置的 SSID；五轮 RSSI 中位数、观测次数和样本年龄来自 C5 本机扫描。</p>' + button('wifi/scan', '扫描 / 读取结果', locked) + '<pre class="code-output" role="status">' + escape(outputs.get(device.id + ':scan') ?? '尚未扫描。') + '</pre>') + '</div>';
  }
  root.innerHTML = `<a class="back-link" href="#/devices">${icon('back')}返回设备管理</a>` + heading(device.name, `${device.id} · ${device.connected ? '在线' : '离线'} · ${sessions[device.id] ? '本页持有会话' : '未获取控制权'}`, sessionButtons(device))
    + `<nav class="workspace-tabs" aria-label="设备功能">${Object.entries(VIEWS).map(([view, label]) => `<a href="${link(device.id, view)}" ${view === route.view ? 'aria-current="page"' : ''}>${label}</a>`).join('')}</nav>` + content;
}
async function readStatus(id: string): Promise<void> { const value = await api<C5Status>(path(id)); details.set(id, value); const device = devices.find((d) => d.id === id); if (device) { device.info = value.info; device.ota = value.ota; } }
function cacheOta(id: string, ota: C5Ota): void {
  const detail = details.get(id); if (detail) details.set(id, { ...detail, ota });
  const device = devices.find((item) => item.id === id); if (device) device.ota = ota;
}
async function refresh({ discover = false, paint = true } = {}) {
  if (refreshing) return; refreshing = true;
  try {
    const value = await api<C5DeviceList>('/api/v1/c5' + (discover ? '/discover' : ''), discover ? 'POST' : 'GET'); devices = value.devices; discoveryError = value.discoveryError;
    for (const [id, session] of Object.entries(sessions)) if (!devices.some((d) => d.id === id) || !session || session.deadline <= Date.now()) delete sessions[id];
    remember();
  } finally { refreshing = false; }
  if (paint && !busy) render();
}
async function mutate(work: () => Promise<void>): Promise<void> {
  if (busy) return; const focus = focusState(), hash = location.hash; saveDraft(); busy = true; render();
  try { await work(); await refresh({ paint: false }); } catch (caught: unknown) { const error = caught as GatewayError; if (error.code === 'session_expired' || error.code === 'probe_4') { const id = currentDeviceId(); if (id) delete sessions[id]; remember(); } report(caught instanceof Error ? caught.message : String(caught), true); } finally { busy = false; render(); if (location.hash === hash) restoreFocus(focus); }
}
async function command<T = C5Exchange>(id: string, action: string, body: Record<string, unknown> = {}): Promise<T> { return api<T>(path(id, '/' + action), 'POST', { sessionId: sessions[id]?.id, ...body }); }
function output(id: string, type: string, value: C5Exchange): void { outputs.set(id + ':' + type, `${value.data || '(空响应)'} · ${value.byteLength} B · ${value.transactionMs} ms`); }
root.addEventListener('click', (event) => {
  const origin = event.target; if (!(origin instanceof Element)) return;
  const target = origin.closest<HTMLButtonElement>('button[data-action]'); if (!target || target.disabled) return;
  const action = target.dataset.action; if (!action) return;
  const id = target.dataset.id ?? currentDeviceId();
  return mutate(async () => {
    if (action === 'discover') { await refresh({ discover: true, paint: false }); report('设备发现完成。'); return; }
    if (action === 'clear') { activity = []; return; }
    if (action === 'acquire') { sessions[id] = await api<C5Session>(path(id, '/session'), 'POST'); remember(); report('已获取设备会话。'); return; }
    if (action === 'release') { await api(path(id, '/session/' + encodeURIComponent(sessions[id].id)), 'DELETE'); delete sessions[id]; remember(); report('会话已释放。'); return; }
    if (action === 'status') { await readStatus(id); drafts.delete(JSON.stringify(route)); report('设备状态已更新。'); return; }
    if (action === 'disable') { await api(path(id, '/pins'), 'PUT', { sessionId: sessions[id]?.id, protocol: Number(target.dataset.protocol), hz: 0, pins: [] }); await readStatus(id); drafts.delete(JSON.stringify(route)); report('协议引脚已释放。'); return; }
    if (action.startsWith('adapter-')) {
      if (action === 'adapter-start') { const value = await command<C5Adapter>(id, 'adapters', { type: drafts.get(JSON.stringify(route))?.['adapter-type'] ?? 'openocd' }); outputs.set(id + ':adapter', value.configuration); report('本机适配器已启动。'); }
      else { await command(id, 'adapters/stop'); outputs.delete(id + ':adapter'); report('适配器已停止。'); } return;
    }
    const ota = details.get(id)?.ota ?? selected()?.ota;
    const value = await command(id, action, action.startsWith('ota/') ? { jobId: ota?.jobId } : {});
    if (action === 'wifi/scan') { outputs.set(id + ':scan', JSON.stringify(decodeScan(value.data), null, 2)); report('扫描结果已读取。'); }
    else if (action === 'reboot' || action === 'ota/activate') { delete sessions[id]; details.delete(id); remember(); report('设备正在重启，请重新发现后获取会话。'); }
    else if (action === 'ota/abort') { cacheOta(id, { jobId: '0', phase: 0, received: 0, checkpoint: 0, total: 0, pendingBoot: false }); await readStatus(id); report('更新任务已中止。'); }
    else { await readStatus(id); report('操作已完成。'); }
  });
});
function decodeScan(hex: string): C5ScanResult[] {
  const bytes = Uint8Array.from(hex.match(/../g) ?? [], (pair) => parseInt(pair, 16)), view = new DataView(bytes.buffer);
  if (bytes.length < 4 || view.getUint32(0, true) > 16 || bytes.length !== 4 + view.getUint32(0, true) * 16) throw new Error('扫描响应长度无效。');
  return Array.from({ length: view.getUint32(0, true) }, (_, index) => { const offset = 4 + index * 16; return { bssid: [...bytes.slice(offset, offset + 6)].map((b) => b.toString(16).padStart(2, '0')).join(':'), channel: bytes[offset + 6], rssi: view.getInt8(offset + 7), wifi6: Boolean(bytes[offset + 8]), sightings: bytes[offset + 10], ageMs: view.getUint32(offset + 12, true) }; });
}
function upload(id: string, manifest: File, image: File): Promise<C5Ota> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest(); xhr.open('POST', path(id, '/ota')); xhr.timeout = 190000; xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    const session = sessions[id]; if (!session) { reject(new Error('设备会话已过期，请重新获取控制权。')); return; }
    xhr.setRequestHeader('X-TetherBench-Session', session.id);
    xhr.upload.onprogress = (event) => { const progress = document.getElementById('upload-progress'), state = document.getElementById('upload-state'); if (progress?.tagName === 'PROGRESS' && event.lengthComputable) (progress as HTMLProgressElement).value = event.loaded / event.total * 100; if (state) state.textContent = event.loaded + ' B 已交给 Gateway，仍待设备校验。'; };
    const recovery = '请先读取设备 OTA 状态，再重新选择相同 manifest 和镜像；C5 会按已保存检查点续传。不要自动重放其他协议事务。';
    xhr.onerror = () => { const message = 'OTA 连接中断，设备接收结果未知。' + recovery; const state = document.getElementById('upload-state'); if (state) state.textContent = message; reject(new Error(message)); };
    xhr.ontimeout = () => { const message = 'OTA 上传超时，设备接收结果未知。' + recovery; const state = document.getElementById('upload-state'); if (state) state.textContent = message; reject(new Error(message)); };
    xhr.onload = () => {
      try {
        const raw: unknown = JSON.parse(xhr.responseText);
        if (xhr.status !== 200) {
          const body = raw && typeof raw === 'object' ? raw as Record<string, unknown> : {};
          const apiError = body.error && typeof body.error === 'object' ? body.error as Record<string, unknown> : {};
          throw new Error((typeof apiError.message === 'string' ? apiError.message : 'OTA 请求失败（HTTP ' + xhr.status + '）。') + ' ' + recovery);
        }
        if (!raw || typeof raw !== 'object') throw new Error('Gateway 返回了无效的 OTA 状态。' + recovery);
        resolve(raw as C5Ota);
      } catch (error) { reject(error); }
    };
    xhr.send(new Blob([manifest, image]));
  });
}
root.addEventListener('submit', (event) => {
  const origin = event.target; if (!(origin instanceof Element)) return;
  const form = origin.closest<HTMLFormElement>('form[data-form]'); if (!form) return;
  event.preventDefault(); const data = new FormData(form), id = currentDeviceId(), type = form.dataset.form;
  if (!id || !type) return;
  return mutate(async () => {
    if (type === 'pins') {
      const rawProtocol = Number(form.dataset.protocol);
      if (!Object.hasOwn(PROTOCOLS, rawProtocol)) throw new Error('不支持该协议。');
      const protocol = rawProtocol as ProtocolId, device = selected();
      if (!device?.info) throw new Error('设备状态未就绪，请先读取状态。');
      const spec = PROTOCOLS[protocol];
      const value = pinSelection(protocol, spec.roles.map((_, index) => data.get(`pin-${protocol}-${index}`)), Number(data.get(`hz-${protocol}`)), device.info.allowedPins);
      await api(path(id, '/pins'), 'PUT', { sessionId: sessions[id]?.id, ...value }); await readStatus(id); drafts.delete(JSON.stringify(route)); report('引脚配置已应用。');
    } else if (type === 'bus') { const protocol = Number(data.get('protocol')); output(id, type, await command(id, 'bus', { protocol, data: hexText(data.get('data')), address: protocol === 4 ? Number(data.get('address')) : 0, receive: Number(data.get('receive')), timeoutMs: Number(data.get('timeoutMs')) })); report('协议事务已完成。'); }
    else if (type === 'dap') { output(id, type, await command(id, 'dap', { data: hexText(data.get('dap-data')) })); report('CMSIS-DAP 响应已收到。'); }
    else if (type === 'ota') { const manifest = data.get('manifest'), image = data.get('image'); if (!(manifest instanceof File) || !(image instanceof File)) throw new Error('请选择签名 manifest 和 C5 固件镜像。'); if (manifest.size !== 176 || image.size < 288 || image.size > 0x1e0000) throw new Error('manifest 必须为 176 B，镜像须符合 C5 分区大小。'); cacheOta(id, await upload(id, manifest, image)); await readStatus(id); report('镜像已由设备校验，可以单独激活。'); }
    else if (type === 'band') {
      const peerId = data.get('peerId');
      const result = await api<C5BandResult>(path(id, '/wifi/negotiate'), 'POST', { sessionId: sessions[id]?.id, peerId }, 60000);
      outputs.set(id + ':band', '信道 ' + result.channel + ' · 当前 ' + result.score.current + ' → 候选 ' + result.score.candidate + ' · 双端五样本已确认');
      report('两台 C5 已确认切换到共同信道。');
    }
  });
});
async function navigate(): Promise<void> {
  saveDraft(); route = parseRoute(location.hash); render();
  scrollTo({ top: 0, behavior: 'instant' });
  requiredElement<HTMLElement>(route.page === 'simulation' ? 'page-title' : 'c5-page-title').focus({ preventScroll: true });
  routeAnimation?.cancel();
  const activeView = requiredElement<HTMLElement>(route.page === 'simulation' ? 'prototype-view' : 'wireless-view');
  routeAnimation = matchMedia('(prefers-reduced-motion: reduce)').matches ? null : activeView.animate?.([{ opacity: 0, transform: 'translate3d(0, 8px, 0) scale(.995)' }, { opacity: 1, transform: 'translate3d(0, 0, 0) scale(1)' }], { duration: 240, easing: 'cubic-bezier(.22, 1, .36, 1)' }) ?? null;
  if (route.page === 'device' && selected()?.connected && !busy) { const hash = location.hash; try { await readStatus(currentDeviceId()); if (location.hash === hash) render(); } catch (error: unknown) { report(error instanceof Error ? error.message : String(error), true); } }
}
addEventListener('hashchange', navigate);
const skipLink = document.querySelector<HTMLAnchorElement>('.skip-link');
skipLink?.addEventListener('click', (event) => { event.preventDefault(); requiredElement<HTMLElement>('main-content').focus(); });
async function tick(): Promise<void> {
  try { if (!busy) {
    for (const [id, session] of Object.entries(sessions)) {
      try { sessions[id] = await api<C5Session>(path(id, '/session/' + encodeURIComponent(session.id) + '/renew'), 'POST'); sessionWarnings.delete(id); }
      catch (caught: unknown) {
        if (isInvalidSession(caught)) { delete sessions[id]; sessionWarnings.delete(id); }
        else if (!sessionWarnings.has(id)) { sessionWarnings.add(id); report('暂时无法续约 ' + id + ' 的会话；本机保留会话标识，并将在下一轮重试。' + errorText(caught), true); }
      }
    }
    remember(); await refresh({ paint: ['devices', 'sessions'].includes(route.page) });
  } } catch (error: unknown) { report(error instanceof Error ? error.message : String(error), true); } finally { setTimeout(tick, 20000); }
}
render();
refresh().then(async () => {
  for (const [id, session] of Object.entries(sessions)) {
    try { sessions[id] = await api<C5Session>(path(id, '/session/' + encodeURIComponent(session.id))); }
    catch (caught: unknown) {
      if (isInvalidSession(caught)) delete sessions[id];
      else report('暂时无法验证 ' + id + ' 的已保存会话；已保留标识供后续续约。' + errorText(caught), true);
    }
  }
  remember(); await navigate();
}).catch((error: unknown) => report(error instanceof Error ? error.message : String(error), true));
setTimeout(tick, 20000);
