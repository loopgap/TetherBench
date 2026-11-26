import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { EventEmitter } from 'node:events';
import { once } from 'node:events';
import { Readable } from 'node:stream';
import type { Duplex } from 'node:stream';
import type { TLSSocket } from 'node:tls';
import type { Server } from 'node:http';
import { test } from 'node:test';
import { C5Client, OP, ProbeError, encodeFrame, decodeResponse } from '../firmware/probe-c5/tools/client.ts';
import { C5Registry, verifyC5CertificateFingerprint } from '../apps/gateway/c5.ts';
import type { C5ClientOptions, C5Profile } from '../apps/gateway/c5.ts';
import { uploadC5 } from '../apps/gateway/c5-routes.ts';
import { createGatewayServer } from '../apps/gateway/server.ts';

function loopbackUrl(server: Server): string {
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('Expected an active TCP listener.');
  return `http://127.0.0.1:${address.port}`;
}

function fixtures() {
  let clock = 1000; const clients: Array<Record<string, any>> = [];
  const profiles: C5Profile[] = [1, 2].map((number) => ({ id: 'probe-' + number, name: 'C5 ' + number, host: '192.168.137.' + number,
    fingerprint: String(number).repeat(64), token: String(number).repeat(64), certificate: Buffer.from('cert-' + number) }));
  const registry = new C5Registry(profiles, {
    now: () => clock, discovery: async () => [],
    clientFactory(options: C5ClientOptions) {
      const client: Record<string, any> = { ...options, lease: 0n, pins: Buffer.alloc(48), received: 0, phase: 0, job: 0n, closed: false,
        close() { this.closed = true; }, async info() { if (this.closed) throw new Error('client is closed'); if (this.fail) throw new Error('connection lost'); return { id: 'c5-00000000000' + options.id.slice(-1), board: 'esp32c5-n4' }; },
        async acquire() { this.lease = 42n; }, async renew() { this.renewed = true; }, async release() { this.lease = 0n; },
        async rpc(op: number, data: Buffer = Buffer.alloc(0)) {
          if (op === OP.PINS) { data.copy(this.pins, (data[0] - 1) * 12); return Buffer.alloc(0); }
          if (op === OP.CONFIG) return this.pins;
          if (op === OP.BUS) return data.subarray(12);
          if (op === OP.OTA_BEGIN) { this.job = data.readBigUInt64LE(); this.total = data.readUInt32LE(12); this.phase = 1; }
          if (op === OP.OTA_WRITE) { assert.equal(data.readUInt32LE(8), this.received); this.received += data.length - 12; }
          if (op === OP.OTA_FINISH) { assert.equal(this.received, this.total); this.phase = 2; return Buffer.alloc(0); }
          if (new Set<number>([OP.OTA_BEGIN, OP.OTA_WRITE, OP.OTA_STATUS]).has(op)) {
            const state = Buffer.alloc(28); state.writeBigUInt64LE(this.job); state.writeUInt32LE(this.phase, 8);
            state.writeUInt32LE(this.received, 12); state.writeUInt32LE(this.total ?? 0, 20); return state;
          }
          return data;
        } }; clients.push(client); return client as unknown as InstanceType<typeof C5Client>;
    },
  });
  return { registry, clients, advance(ms: number) { clock += ms; } };
}

test('C5 frames reject truncation, mismatched owners and oversized data', () => {
  const wire = encodeFrame(OP.DAP, 42, 51n, Buffer.from([0, 0xf0]), 0x8000);
  assert.deepEqual(decodeResponse(wire, OP.DAP, 42, 51n), Buffer.from([0, 0xf0]));
  for (let index = 0; index < wire.length; index++) assert.throws(() => decodeResponse(wire.subarray(0, index), OP.DAP, 42, 51n));
  assert.throws(() => decodeResponse(wire, OP.DAP, 42, 52n));
  assert.throws(() => encodeFrame(OP.DAP, 1, 0n, Buffer.alloc(4097)));
  assert.throws(() => decodeResponse(encodeFrame(OP.INFO, 1, 0n, Buffer.alloc(0), 0x8004), OP.INFO, 1, 0n), ProbeError);
});

test('C5 profile identity rejects a certificate whose DER fingerprint differs from credentials', () => {
  const certificateDer = Buffer.from('certificate DER fixture');
  const fingerprint = createHash('sha256').update(certificateDer).digest('hex');
  assert.equal(verifyC5CertificateFingerprint(certificateDer, fingerprint.toUpperCase()), fingerprint);
  assert.throws(() => verifyC5CertificateFingerprint(certificateDer, '0'.repeat(64)), /does not match/);
});

test('HTTPS receives a TLS socket only after the provisioned certificate matches', async () => {
  for (const matches of [false, true]) {
    const socket = Object.assign(new EventEmitter(), { destroyed: false, setTimeout: (_timeout: number) => {},
      destroy() { this.destroyed = true; }, getPeerCertificate: () => ({ raw: Buffer.from(matches ? 'correct' : 'wrong') }) }) as unknown as TLSSocket;
    const client = new C5Client({ host: '127.0.0.1', token: 'a'.repeat(64), certificate: Buffer.from('correct'), tlsConnect: () => socket });
    let delivered = false;
    const result = new Promise<{ error: Error | null; value: Duplex }>((resolve) => client.agent.createConnection({}, (error, value) => { delivered = true; resolve({ error, value }); }));
    assert.equal(delivered, false); socket.emit('secureConnect');
    const value = await result;
    assert.equal(Boolean(value.error), !matches); assert.equal(value.value === socket, true);
    assert.equal(socket.destroyed, !matches); client.close();
  }
});

test('C5 sessions isolate devices, reject duplicate owners and recover after transport loss', async () => {
  const { registry, clients, advance } = fixtures();
  try {
    await registry.refresh();
    const first = await registry.acquire('probe-1'), second = await registry.acquire('probe-2');
    await assert.rejects(registry.acquire('probe-1'), { code: 'session_busy' });
    await assert.rejects(registry.command('probe-2', first.id, OP.DAP), { code: 'session_expired' });
    clients[0].fail = true;
    await registry.refresh(); assert.equal(registry.record('probe-1').connected, false);
    clients[0].fail = false;
    await registry.refresh(); assert.equal(registry.record('probe-1').connected, true);
    assert.notEqual(registry.record('probe-1').client, clients[0]);
    assert.equal(registry.record('probe-1').client?.lease, 42n);
    assert.equal(registry.session('probe-1', first.id).id, first.id);
    await registry.release('probe-1', first.id); assert.equal(registry.session('probe-2', second.id).id, second.id);
    advance(61000); await registry.maintain(); assert.equal(registry.record('probe-2').session, null);
    assert.equal(JSON.stringify(registry.list()).includes('token'), false);
  } finally { registry.close(); }
});

function bandFixtures({ failConfirm = null, apRole = null }: { failConfirm?: number | null; apRole?: number | null } = {}) {
  const clients: Array<Record<string, any>> = [], events: Array<[string, number]> = []; let registry: C5Registry, transactionId: bigint | null = null;
  const profiles: C5Profile[] = [1, 2].map((number) => ({ id: 'band-' + number, name: 'Band C5 ' + number, host: '192.168.137.' + number,
    fingerprint: String(number).repeat(64), token: String(number).repeat(64), certificate: Buffer.from('cert-' + number) }));
  registry = new C5Registry(profiles, { now: () => 1000, discovery: async () => [], sleep: async () => {}, clientFactory(options: C5ClientOptions) {
    const number = Number(options.id.slice(-1)), currentChannel = 6, identity = 'c5-00000000000' + number;
    const client: Record<string, any> = { ...options, lease: 0n, closed: false, renewCount: 0, wifi: { ap: apRole === number, channel: currentChannel, rssi: number === 1 ? -80 : -78, phy: 5 }, bandStage: 0,
      close() { this.closed = true; },
      async info() { return { id: identity, board: 'esp32c5-n4', bandStage: this.bandStage, wifi: { ...this.wifi } }; },
      async acquire() { this.lease = BigInt(number); }, async renew() { this.renewCount++; }, async release() { this.lease = 0n; },
      async rpc(op: number, data: Buffer = Buffer.alloc(0)) {
        if (op === OP.CONFIG) return Buffer.alloc(48);
        if (op === OP.WIFI_SCAN) {
          const scan = Buffer.alloc(20); scan.writeUInt32LE(1); Buffer.from([number, 2, 3, 4, 5, 6]).copy(scan, 4);
          scan[10] = 40; scan.writeInt8(number === 1 ? -50 : -52, 11); scan[12] = 1; scan[13] = 3; scan[14] = 5; scan.writeUInt32LE(50, 16); return scan;
        }
        if (op === OP.BAND_PREPARE) {
          assert.equal(data.length, 60); assert.equal(data[8], 40); assert.equal(data[9], 6);
          const receivedId = data.readBigUInt64LE();
          if (transactionId === null) transactionId = receivedId; else assert.equal(receivedId, transactionId);
          events.push(['prepare', number]); this.transaction = receivedId; this.bandStage = 1;
          await assert.rejects(registry.run('band-' + (number === 1 ? 2 : 1), async () => {}), { code: 'device_busy' });
          return Buffer.alloc(0);
        }
        if (op === OP.BAND_COMMIT) { events.push(['commit', number]); this.wifi.channel = 40; this.bandStage = 3; return Buffer.alloc(0); }
        if (op === OP.BAND_CONFIRM) {
          events.push(['confirm', number]);
          if (failConfirm === number) throw new ProbeError(8);
          this.bandStage = 0; return Buffer.alloc(0);
        }
        if (op === OP.BAND_FINALIZE) { events.push(['finalize', number]); this.bandStage = 0; return Buffer.alloc(0); }
        if (op === OP.BAND_CANCEL) {
          events.push(['cancel', number]); if (this.wifi.channel === 40) this.wifi.channel = currentChannel;
          this.bandStage = 0; return Buffer.alloc(0);
        }
        return Buffer.alloc(0);
      } };
    clients.push(client); return client as unknown as InstanceType<typeof C5Client>;
  } });
  return { registry, clients, events };
}

test('dual-C5 band change holds both leases, measures a common AP and confirms both devices', async () => {
  const { registry, clients, events } = bandFixtures();
  try {
    await registry.refresh();
    const first = await registry.acquire('band-1'), second = await registry.acquire('band-2');
    const result = await registry.negotiateBand('band-1', first.id, 'band-2', second.id);
    assert.equal(result.channel, 40); assert.deepEqual(result.previousChannels, [6, 6]);
    assert.deepEqual(result.score, { current: 20, candidate: 76 });
    assert.deepEqual(events.filter(([kind]) => ['prepare', 'commit', 'confirm', 'finalize'].includes(kind)).map(([kind, number]) => kind + number),
      ['prepare1', 'prepare2', 'commit1', 'commit2', 'confirm1', 'confirm2', 'finalize1', 'finalize2']);
    assert.deepEqual(clients.map((client) => client.wifi.channel), [40, 40]);
  } finally { registry.close(); }
});

test('dual-C5 channel negotiation rejects either AP-role device before renewing leases or switching', async () => {
  for (const apRole of [1, 2]) {
    const { registry, clients, events } = bandFixtures({ apRole });
    try {
      await registry.refresh();
      const first = await registry.acquire('band-1'), second = await registry.acquire('band-2');
      await assert.rejects(registry.negotiateBand('band-1', first.id, 'band-2', second.id), { code: 'wifi6_sta_required' });
      assert.deepEqual(clients.map((client) => client.renewCount), [0, 0]);
      assert.deepEqual(clients.map((client) => client.wifi.channel), [6, 6]);
      assert.deepEqual(events, []);
      assert.equal(registry.record('band-1').busy, false);
      assert.equal(registry.record('band-2').busy, false);
    } finally { registry.close(); }
  }
});

test('dual-C5 coordinator compensates both devices after one confirmation fails', async () => {
  const { registry, clients, events } = bandFixtures({ failConfirm: 2 });
  try {
    await registry.refresh();
    const first = await registry.acquire('band-1'), second = await registry.acquire('band-2');
    await assert.rejects(registry.negotiateBand('band-1', first.id, 'band-2', second.id), { code: 'probe_8' });
    assert.deepEqual(clients.map((client) => client.wifi.channel), [6, 6]);
    assert.equal(events.filter(([kind]) => kind === 'cancel').length, 2);
  } finally { registry.close(); }
});

test('C5 Wi-Fi route obtains a temporary peer lease and releases it after negotiation', async () => {
  const { registry, clients } = bandFixtures(); const server = createGatewayServer({ c5Registry: registry });
  server.listen(0, '127.0.0.1'); await once(server, 'listening');
  const base = loopbackUrl(server);
  try {
    await registry.refresh(); const first = await registry.acquire('band-1');
    const response = await fetch(base + '/api/v1/c5/band-1/wifi/negotiate', { method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ sessionId: first.id, peerId: 'band-2' }) });
    assert.equal(response.status, 200); assert.equal((await response.json() as { channel: number }).channel, 40);
    assert.equal(clients[1].lease, 0n); assert.equal(registry.record('band-2').session, null);
    assert.equal(clients[0].lease, 1n); assert.equal(registry.record('band-1').session?.id, first.id);
  } finally { server.closeAllConnections(); await new Promise<void>((resolve) => server.close(() => resolve())); }
});

test('Gateway routes encode bounded GPIO and bus transactions and enforce ownership', async () => {
  const { registry, clients } = fixtures(); await registry.refresh();
  const server = createGatewayServer({ c5Registry: registry }); server.listen(0, '127.0.0.1'); await once(server, 'listening');
  const base = loopbackUrl(server);
  const api = async (path: string, method = 'GET', body?: unknown): Promise<{ status: number; body: any }> => {
    const response = await fetch(base + '/api/v1/c5' + path, { method,
      headers: body ? { 'Content-Type': 'application/json' } : {}, body: body ? JSON.stringify(body) : undefined });
    return { status: response.status, body: await response.json() };
  };
  try {
    const session = (await api('/probe-1/session', 'POST')).body;
    assert.equal((await api('/probe-1/pins', 'PUT', { sessionId: session.id, protocol: 1, hz: 1000000, pins: [2, 3] })).status, 200);
    assert.equal(clients[0].pins.readInt8(10), -1);
    const reply = await api('/probe-1/bus', 'POST', { sessionId: session.id, protocol: 2, data: 'aabb', receive: 2 });
    assert.equal(reply.body.data, 'aabb');
    assert.equal((await api('/probe-1/bus', 'POST', { sessionId: session.id, protocol: 2, data: 'abc' })).status, 400);
    assert.equal((await api('/probe-2/dap', 'POST', { sessionId: session.id, data: '00f0' })).status, 409);
    const listing = (await api('')).body; assert.equal(listing.devices.length, 2);
    assert.equal('session' in listing.devices[0], false);
  } finally { server.closeAllConnections(); await new Promise<void>((resolve) => server.close(() => resolve())); }
});

test('OTA upload streams bounded blocks and resumes from the device offset', async () => {
  const { registry, clients } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const manifest = Buffer.alloc(176), image = Buffer.alloc(20000, 0x33);
    manifest.writeBigUInt64LE(42n); manifest.writeUInt32LE(image.length, 12); clients[0].received = 4000;
    const wire = Buffer.concat([manifest, image]);
    const chunks = []; for (let offset = 0; offset < wire.length; offset += 777) chunks.push(wire.subarray(offset, offset + 777));
    const state = await uploadC5(registry, 'probe-1', session.id, Readable.from(chunks), wire.length);
    assert.equal(state.phase, 2); assert.equal(state.received, image.length);
    await assert.rejects(uploadC5(registry, 'probe-1', 'wrong-owner', Readable.from([]), wire.length), { code: 'session_expired' });
    await assert.rejects(uploadC5(registry, 'probe-1', session.id, Readable.from([wire.subarray(0, 100)]), wire.length), { code: 'ota_truncated' });
  } finally { registry.close(); }
});

test('an interrupted OTA request preserves the healthy C5 connection and session', async () => {
  const { registry, clients } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const input = Readable.from((async function* () { yield Buffer.from([0]); throw new Error('request disconnected'); })());
    await assert.rejects(uploadC5(registry, 'probe-1', session.id, input, 464), { code: 'ota_input_interrupted' });
    assert.equal(registry.record('probe-1').connected, true);
    assert.equal(registry.record('probe-1').session?.id, session.id);
    assert.equal(clients[0].closed, false);
  } finally { registry.close(); }
});
