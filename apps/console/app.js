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
let heartbeatTimer = null;

async function request(path, { method = 'GET', body } = {}) {
  const response = await fetch(`${api}${path}`, {
    method,
    headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const result = response.status === 204 ? null : await response.json();
  if (!response.ok) throw new Error(result?.error?.message ?? `Request failed (${response.status}).`);
  return result;
}

function announce(message) {
  elements.announcer.textContent = message;
}

function render() {
  elements['device-count'].textContent = `${devices.length} connected`;
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
    row.setAttribute('aria-pressed', String(device.id === selectedDeviceId));
    const main = document.createElement('span');
    main.className = 'device-main';
    const name = document.createElement('span');
    name.className = 'device-name';
    name.textContent = device.name;
    const id = document.createElement('span');
    id.className = 'device-id';
    id.textContent = device.id;
    const capabilities = document.createElement('span');
    capabilities.className = 'device-capabilities';
    capabilities.textContent = device.capabilities.join(' · ') || 'No capabilities reported';
    main.append(name, id, capabilities);
    const status = document.createElement('span');
    status.className = 'device-status';
    status.textContent = 'Connected';
    row.append(main, status);
    row.addEventListener('click', () => {
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
  if (!selected) {
    selectedDeviceId = null;
  }
  if (session && !devices.some((device) => device.id === session.deviceId)) session = null;
  elements['selected-name'].textContent = selected?.name ?? 'Select a device';
  elements['selected-id'].textContent = selected?.id ?? 'No device selected';
  elements['session-state'].textContent = session
    ? `Session active · ${session.deviceId}`
    : selected?.sessionActive ? 'Session held by another client' : 'No active session';
  elements['session-state'].classList.toggle('active', Boolean(session));
  elements['acquire-button'].disabled = !selected || Boolean(session) || selected.sessionActive;
  elements['release-button'].disabled = !session;
  elements['disconnect-button'].disabled = !selected;
  elements['exchange-button'].disabled = !session;
  elements['connect-button'].disabled = Boolean(browserProbeId);
}

async function refresh() {
  const health = await request('/health');
  devices = (await request('/devices')).devices;
  elements['project-version'].textContent = health.version;
  elements['service-label'].textContent = `Gateway online · ${health.sessions} session${health.sessions === 1 ? '' : 's'}`;
  elements['service-dot'].classList.add('online');
  render();
}

elements['refresh-button'].addEventListener('click', async () => {
  try {
    await refresh();
    announce('Device list refreshed.');
  } catch (error) {
    elements['service-label'].textContent = 'Gateway unavailable';
    elements['service-dot'].classList.remove('online');
    announce(error.message);
  }
});

elements['connect-button'].addEventListener('click', async () => {
  try {
    const id = `browser-${crypto.randomUUID().slice(0, 8)}`;
    const device = await request('/devices', {
      method: 'POST',
      body: { id, name: 'Browser test probe', capabilities: ['cmsis-dap', 'uart'] },
    });
    browserProbeId = device.id;
    selectedDeviceId = device.id;
    if (heartbeatTimer) clearInterval(heartbeatTimer);
    heartbeatTimer = setInterval(() => {
      request(`/devices/${encodeURIComponent(browserProbeId)}/heartbeat`, { method: 'POST' })
        .catch((error) => announce(`Test probe heartbeat failed: ${error.message}`));
    }, 5000);
    await refresh();
    announce('Browser test probe connected.');
  } catch (error) {
    announce(error.message);
  }
});

elements['acquire-button'].addEventListener('click', async () => {
  if (!selectedDeviceId) return;
  try {
    session = await request(`/devices/${encodeURIComponent(selectedDeviceId)}/sessions`, { method: 'POST' });
    elements['exchange-result'].textContent = 'Session acquired. No hardware is attached.';
    await refresh();
    announce('Debug session acquired.');
  } catch (error) {
    announce(error.message);
  }
});

elements['release-button'].addEventListener('click', async () => {
  if (!session) return;
  try {
    await request(`/sessions/${encodeURIComponent(session.id)}`, { method: 'DELETE' });
    session = null;
    render();
    await refresh();
    announce('Debug session released.');
  } catch (error) {
    announce(error.message);
  }
});

elements['disconnect-button'].addEventListener('click', async () => {
  if (!selectedDeviceId) return;
  try {
    const id = selectedDeviceId;
    await request(`/devices/${encodeURIComponent(id)}`, { method: 'DELETE' });
    if (id === browserProbeId) {
      browserProbeId = null;
      clearInterval(heartbeatTimer);
      heartbeatTimer = null;
    }
    selectedDeviceId = null;
    if (session?.deviceId === id) session = null;
    elements['exchange-result'].textContent = 'No data exchanged.';
    await refresh();
    announce('Probe disconnected and any session released.');
  } catch (error) {
    announce(error.message);
  }
});

elements['exchange-form'].addEventListener('submit', async (event) => {
  event.preventDefault();
  if (!session) return;
  try {
    const result = await request(`/sessions/${encodeURIComponent(session.id)}/exchange`, {
      method: 'POST',
      body: { data: elements['exchange-data'].value },
    });
    elements['exchange-result'].textContent = `Echo (${result.byteLength} bytes): ${result.data}`;
    announce('Simulated data exchange completed.');
  } catch (error) {
    elements['exchange-result'].textContent = error.message;
    announce(error.message);
  }
});

refresh().catch((error) => {
  elements['service-label'].textContent = 'Gateway unavailable';
  elements['service-dot'].classList.remove('online');
  announce(error.message);
});
