import { fileURLToPath } from 'node:url';

export async function startSimulator({
  baseUrl = process.env.TETHERBENCH_URL ?? 'http://127.0.0.1:4173',
  deviceId = process.env.TETHERBENCH_DEVICE_ID ?? 'sim-' + process.pid,
  intervalMs = 5000, timeoutMs = 5000,
  onError = (error) => console.error('Heartbeat failed: ' + error.message),
} = {}) {
  const devicePath = '/api/v1/devices/' + encodeURIComponent(deviceId);
  let stopped = false;
  let timer;
  let pending;
  async function request(path, options = {}) {
    const response = await fetch(new URL(path, baseUrl), { ...options, signal: AbortSignal.timeout(timeoutMs) });
    const value = response.status === 204 ? null : await response.json();
    if (!response.ok) {
      const error = new Error(value?.error?.message ?? 'Gateway returned ' + response.status + '.');
      error.status = response.status;
      throw error;
    }
    return value;
  }
  const register = () => request('/api/v1/devices', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: deviceId, name: 'Independent mock probe', capabilities: ['cmsis-dap', 'uart'] }),
  });
  async function beat() {
    try { await request(devicePath + '/heartbeat', { method: 'POST' }); }
    catch (error) {
      if (error.status === 404 && !stopped) await register();
      else throw error;
    }
  }
  function schedule() {
    if (stopped) return;
    timer = setTimeout(() => { pending = beat().catch(onError).finally(schedule); }, intervalMs);
  }
  await register();
  schedule();
  return async function stop() {
    stopped = true;
    clearTimeout(timer);
    await pending;
    await request(devicePath, { method: 'DELETE' }).catch((error) => {
      if (error.status !== 404) throw error;
    });
  };
}
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const stop = await startSimulator();
  console.log('Mock probe registered. Press Ctrl+C to stop.');
  await new Promise((resolve) => { process.once('SIGINT', resolve); process.once('SIGTERM', resolve); });
  await stop();
  console.log('Mock probe disconnected.');
}
