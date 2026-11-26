import { fileURLToPath } from 'node:url';

export interface SimulatorOptions {
  baseUrl?: string;
  deviceId?: string;
  intervalMs?: number;
  timeoutMs?: number;
  onError?: (error: unknown) => void;
}

class SimulatorRequestError extends Error {
  readonly status: number;
  constructor(status: number, message: string) { super(message); this.name = 'SimulatorRequestError'; this.status = status; }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

export async function startSimulator({
  baseUrl = process.env.TETHERBENCH_URL ?? 'http://127.0.0.1:4173',
  deviceId = process.env.TETHERBENCH_DEVICE_ID ?? 'sim-' + process.pid,
  intervalMs = 5000, timeoutMs = 5000,
  onError = (error: unknown) => console.error('Heartbeat failed: ' + (error instanceof Error ? error.message : String(error))),
}: SimulatorOptions = {}): Promise<() => Promise<void>> {
  if (!Number.isSafeInteger(intervalMs) || intervalMs < 1 || !Number.isSafeInteger(timeoutMs) || timeoutMs < 1) {
    throw new RangeError('Simulator interval and request timeout must be positive safe integers.');
  }
  const devicePath = '/api/v1/devices/' + encodeURIComponent(deviceId);
  let stopped = false;
  let timer: ReturnType<typeof setTimeout> | undefined;
  let pending: Promise<void> | undefined;
  async function request(path: string, options: RequestInit = {}): Promise<unknown> {
    const response = await fetch(new URL(path, baseUrl), { ...options, signal: AbortSignal.timeout(timeoutMs) });
    let value: unknown = null;
    if (response.status !== 204) {
      try { value = await response.json() as unknown; }
      catch { throw new Error('Gateway returned a non-JSON response.'); }
    }
    if (!response.ok) {
      const details = isRecord(value) && isRecord(value.error) ? value.error.message : undefined;
      throw new SimulatorRequestError(response.status, typeof details === 'string' ? details : 'Gateway returned ' + response.status + '.');
    }
    return value;
  }
  const register = async (): Promise<void> => { await request('/api/v1/devices', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: deviceId, name: 'Independent mock probe', capabilities: ['cmsis-dap', 'uart'] }),
  }); };
  async function beat(): Promise<void> {
    try { await request(devicePath + '/heartbeat', { method: 'POST' }); }
    catch (error: unknown) {
      if (error instanceof SimulatorRequestError && error.status === 404 && !stopped) await register();
      else throw error;
    }
  }
  function schedule(): void {
    if (stopped) return;
    timer = setTimeout(() => { pending = beat().catch(onError).finally(schedule); }, intervalMs);
  }
  await register();
  schedule();
  return async function stop() {
    stopped = true;
    clearTimeout(timer);
    await pending;
    await request(devicePath, { method: 'DELETE' }).catch((error: unknown) => {
      if (!(error instanceof SimulatorRequestError) || error.status !== 404) throw error;
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
