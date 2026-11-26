import assert from 'node:assert/strict';
import { once } from 'node:events';
import { connect } from 'node:net';
import type { Socket } from 'node:net';
import { test } from 'node:test';
import { C5Registry } from '../apps/gateway/c5.ts';
import type { C5ClientOptions, C5Profile } from '../apps/gateway/c5.ts';
import { C5Client } from '../firmware/probe-c5/tools/client.ts';
import { C5Adapters, startDapTcp } from '../apps/gateway/c5-adapters.ts';
import { OP } from '../firmware/probe-c5/tools/client.ts';

const frame = (payload: Buffer): Buffer => { const header = Buffer.alloc(8); header.writeUInt32LE(0x00504144); header.writeUInt16LE(payload.length, 4); header[6] = 1; return Buffer.concat([header, payload]); };
async function receive(socket: Socket, count: number): Promise<Buffer> {
  const parts = []; let size = 0;
  while (size < count) { const part = socket.read(count - size); if (part) { parts.push(part); size += part.length; } else { if (socket.readableEnded) throw new Error('Disconnected'); await once(socket, 'readable'); } }
  return Buffer.concat(parts);
}

test('OpenOCD TCP handles segmentation, rejects a second client and closes malformed requests', async () => {
  let calls = 0;
  const adapter = await startDapTcp(async (data) => { calls++; return Buffer.concat([data, Buffer.from([1])]); });
  const socket = connect(adapter.port, '127.0.0.1'); await once(socket, 'connect');
  try {
    const request = frame(Buffer.from([0, 0xf0])); socket.write(request.subarray(0, 3)); socket.write(request.subarray(3));
    const response = await receive(socket, 11); assert.equal(response[6], 2); assert.equal(response.readUInt16LE(4), 3); assert.equal(calls, 1);
    const extra = connect(adapter.port, '127.0.0.1'); await once(extra, 'close');
    socket.write(Buffer.alloc(8)); await once(socket, 'close'); assert.equal(calls, 1);
  } finally { socket.destroy(); adapter.close(); }
});

test('Adapter owns its C5 resources, waits for maintenance and closes when its session expires', async () => {
  let now = 1000, lease = 0n, calls = 0;
  const profile: C5Profile = { id: 'test', name: 'C5', host: '127.0.0.1', fingerprint: 'a'.repeat(64), token: 'b'.repeat(64), certificate: Buffer.from('certificate') };
  const registry = new C5Registry([profile], { now: () => now, discovery: async () => [], clientFactory: (_options: C5ClientOptions) => ({
    host: '127.0.0.1', get lease() { return lease; }, set lease(value) { lease = value; }, close() {},
    async info() { return { id: 'c5-001122334455' }; }, async acquire() { lease = 42n; }, async renew() {}, async release() { lease = 0n; },
    async rpc(op: number, data: Buffer = Buffer.alloc(0)) { assert.equal(op, OP.DAP); calls++; return Buffer.from([data[0] ?? 0, 1, 1]); },
  } as unknown as InstanceType<typeof C5Client>) });
  const adapters = new C5Adapters(registry); let socket: Socket | undefined;
  try {
    await registry.refresh(); const session = await registry.acquire('test');
    const info = await adapters.start('test', session.id, 'openocd'); assert.match(info.configuration, /cmsis-dap tcp host 127/);
    await assert.rejects(registry.command('test', session.id, OP.DAP, Buffer.from([0, 0xf0])), { code: 'adapter_active' });
    await assert.rejects(adapters.start('test', session.id, 'openocd'), { code: 'adapter_active' });
    let unlock!: () => void; const held = registry.run('test', () => new Promise<void>((resolve) => { unlock = resolve; }));
    socket = connect(info.port, '127.0.0.1'); await once(socket, 'connect'); socket.write(frame(Buffer.from([0, 0xf0])));
    unlock(); await held; assert.equal((await receive(socket, 11))[8], 0); assert.equal(calls, 1);
    now += 61000; const closed = once(socket, 'close'); adapters.sweep(); await closed; assert.equal(adapters.list('test').length, 0);
  } finally { socket?.destroy(); adapters.close(); registry.close(); }
});
