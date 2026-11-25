const api = '/api/v1';
const elements = Object.fromEntries([
  'service-dot', 'service-label', 'project-version', 'refresh-button', 'connect-button', 'device-count', 'device-list',
  'selected-name', 'selected-id', 'session-state', 'acquire-button', 'release-button',
  'disconnect-button', 'exchange-form', 'exchange-data', 'exchange-button', 'exchange-result', 'announcer',
].map((id) => [id, document.getElementById(id)]));
let devices = [];
let selectedDeviceId = null;
let session = null;
let browserProbeId = null;
let savedSessionId = null;
let heartbeatTimer = null;
let refreshPromise = null;
let busy = false;

function announce(message) { elements.announcer.textContent = message; }
try {
  savedSessionId = sessionStorage.getItem('tetherbench.session');
  browserProbeId = sessionStorage.getItem('tetherbench.browserProbe');
} catch {
  announce('Browser storage is unavailable; sessions cannot survive a page reload.');
}
function remember(key, value) {
  try {
    if (value) sessionStorage.setItem(key, value);
    else sessionStorage.removeItem(key);
  } catch {
    announce('Browser storage is unavailable; sessions cannot survive a page reload.');
  }
}
function setSession(value) {
  session = value;
  savedSessionId = value?.id ?? null;
  remember('tetherbench.session', savedSessionId);
}
function clearBrowserProbe() {
  browserProbeId = null;
  remember('tetherbench.browserProbe', null);
  clearTimeout(heartbeatTimer);
  heartbeatTimer = null;
}
async function request(path, { method = 'GET', body } = {}) {
  const response = await fetch(api + path, {
    method,
    headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(5000),
  });
  const result = response.status === 204 ? null : await response.json();
  if (!response.ok) {
    const error = new Error(result?.error?.message ?? 'Request failed (' + response.status + ').');
    error.status = response.status;
    throw error;
  }
  return result;
}
function heartbeat() {
  if (!browserProbeId || heartbeatTimer) return;
  heartbeatTimer = setTimeout(async () => {
    const id = browserProbeId;
    try {
      await request('/devices/' + encodeURIComponent(id) + '/heartbeat', { method: 'POST' });
    } catch (error) {
      if (error.status === 404 && browserProbeId === id) {
        clearBrowserProbe();
        if (session?.deviceId === id) setSession(null);
        render();
      }
      announce('Test probe heartbeat failed: ' + error.message);
    } finally {
      heartbeatTimer = null;
      heartbeat();
    }
  }, 5000);
}
function render() {
  elements['device-count'].textContent = devices.length + ' connected';
  elements['device-list'].replaceChildren();
  if (devices.length === 0) {
    const empty = document.createElement('p');
    empty.className = 'empty-state';
    empty.textContent = 'No probes connected.';
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
      ['device-capabilities', device.capabilities.join(' · ') || 'No capabilities reported'],
    ]) {
      const text = document.createElement('span');
      text.className = className;
      text.textContent = value;
      main.append(text);
    }
    const status = document.createElement('span');
    status.className = 'device-status';
    status.textContent = 'Connected';
    row.append(main, status);
    row.addEventListener('click', () => {
      if (busy) return;
      if (session && session.deviceId !== device.id) {
        announce('Release the active session before selecting another probe.');
        return;
      }
      selectedDeviceId = device.id;
      render();
    });
    elements['device-list'].append(row);
  }
  const selected = devices.find((device) => device.id === selectedDeviceId);
  if (!selected) selectedDeviceId = null;
  if (session && !devices.some((device) => device.id === session.deviceId)) setSession(null);
  elements['selected-name'].textContent = selected?.name ?? 'Select a device';
  elements['selected-id'].textContent = selected?.id ?? 'No device selected';
  elements['session-state'].textContent = session ? 'Session active · ' + session.deviceId
    : selected?.sessionActive ? 'Session held by another client' : 'No active session';
  elements['session-state'].classList.toggle('active', Boolean(session));
  elements['acquire-button'].disabled = busy || !selected || Boolean(session) || selected.sessionActive;
  elements['release-button'].disabled = busy || !session;
  elements['disconnect-button'].disabled = busy || !selected;
  elements['exchange-button'].disabled = busy || !session;
  elements['connect-button'].disabled = busy || Boolean(browserProbeId);
}
async function loadDevices() {
  const health = await request('/health');
  devices = (await request('/devices')).devices;
  if (savedSessionId) {
    try {
      setSession(await request('/sessions/' + encodeURIComponent(savedSessionId)));
      selectedDeviceId = session.deviceId;
    } catch (error) {
      if (error.status !== 404) throw error;
      setSession(null);
    }
  }
  if (browserProbeId && !devices.some((device) => device.id === browserProbeId)) clearBrowserProbe();
  heartbeat();
  elements['project-version'].textContent = health.version;
  elements['service-label'].textContent = 'Gateway online · ' + health.sessions + ' session' + (health.sessions === 1 ? '' : 's');
  elements['service-dot'].classList.add('online');
  render();
}
function refresh() {
  refreshPromise ??= loadDevices().finally(() => { refreshPromise = null; });
  return refreshPromise;
}
function unavailable(error) {
  elements['service-label'].textContent = 'Gateway unavailable';
  elements['service-dot'].classList.remove('online');
  announce(error.message);
}
async function mutate(action) {
  if (busy) return;
  busy = true;
  render();
  try { await action(); } catch (error) { announce(error.message); }
  finally { busy = false; render(); }
}
elements['refresh-button'].addEventListener('click', () => refresh()
  .then(() => announce('Device list refreshed.')).catch(unavailable));
elements['connect-button'].addEventListener('click', () => mutate(async () => {
  if (browserProbeId) return;
  const device = await request('/devices', {
    method: 'POST', body: { id: 'browser-' + crypto.randomUUID().slice(0, 8),
      name: 'Browser test probe', capabilities: ['cmsis-dap', 'uart'] },
  });
  browserProbeId = device.id;
  remember('tetherbench.browserProbe', browserProbeId);
  selectedDeviceId = device.id;
  heartbeat();
  await refresh();
  announce('Browser test probe connected.');
}));
elements['acquire-button'].addEventListener('click', () => mutate(async () => {
  if (!selectedDeviceId || session) return;
  setSession(await request('/devices/' + encodeURIComponent(selectedDeviceId) + '/sessions', { method: 'POST' }));
  elements['exchange-result'].textContent = 'Session acquired. No hardware is attached.';
  await refresh();
  announce('Debug session acquired.');
}));
elements['release-button'].addEventListener('click', () => mutate(async () => {
  if (!session) return;
  try { await request('/sessions/' + encodeURIComponent(session.id), { method: 'DELETE' }); }
  catch (error) { if (error.status !== 404) throw error; }
  setSession(null);
  await refresh();
  announce('Debug session released.');
}));
elements['disconnect-button'].addEventListener('click', () => mutate(async () => {
  if (!selectedDeviceId) return;
  const id = selectedDeviceId;
  await request('/devices/' + encodeURIComponent(id), { method: 'DELETE' });
  if (id === browserProbeId) clearBrowserProbe();
  if (session?.deviceId === id) setSession(null);
  selectedDeviceId = null;
  elements['exchange-result'].textContent = 'No data exchanged.';
  await refresh();
  announce('Probe disconnected and any session released.');
}));
elements['exchange-form'].addEventListener('submit', (event) => {
  event.preventDefault();
  return mutate(async () => {
    if (!session) return;
    const result = await request('/sessions/' + encodeURIComponent(session.id) + '/exchange', {
      method: 'POST', body: { data: elements['exchange-data'].value },
    });
    elements['exchange-result'].textContent = 'Echo (' + result.byteLength + ' bytes): ' + result.data;
    announce('Simulated data exchange completed.');
  });
});
refresh().catch(unavailable);
