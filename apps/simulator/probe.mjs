const baseUrl = process.env.TETHERBENCH_URL ?? 'http://127.0.0.1:4173';
const deviceId = process.env.TETHERBENCH_DEVICE_ID ?? `sim-${process.pid}`;
const devicePath = `/api/v1/devices/${encodeURIComponent(deviceId)}`;

async function request(path, options = {}) {
  const response = await fetch(new URL(path, baseUrl), options);
  const value = response.status === 204 ? null : await response.json();
  if (!response.ok) throw new Error(value?.error?.message ?? `Gateway returned ${response.status}.`);
  return value;
}

await request('/api/v1/devices', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    id: deviceId,
    name: 'Independent mock probe',
    capabilities: ['cmsis-dap', 'uart'],
  }),
});
console.log(`Mock probe ${deviceId} registered at ${baseUrl}. Press Ctrl+C to stop.`);

const heartbeat = setInterval(() => {
  request(`${devicePath}/heartbeat`, { method: 'POST' })
    .catch((error) => console.error(`Heartbeat failed: ${error.message}`));
}, 5000);

await new Promise((resolve) => {
  process.once('SIGINT', resolve);
  process.once('SIGTERM', resolve);
});
clearInterval(heartbeat);
try {
  await request(devicePath, { method: 'DELETE' });
  console.log('Mock probe disconnected.');
} catch (error) {
  console.error(`Could not unregister mock probe: ${error.message}`);
  process.exitCode = 1;
}
