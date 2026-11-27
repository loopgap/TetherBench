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
import { uploadC5, uploadTarget } from '../apps/gateway/c5-routes.ts';
import { createGatewayServer } from '../apps/gateway/server.ts';

function loopbackUrl(server: Server): string {
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('Expected an active TCP listener.');
  return `http://127.0.0.1:${address.port}`;
}

function fixtures() {
  let clock = 1000; const clients: Array<Record<string, any>> = [];
  const targetInfo = Buffer.alloc(180); targetInfo.writeUInt16LE(1, 0); targetInfo.writeUInt32LE(0xff, 4); targetInfo.writeUInt32LE(0x100000, 8);
  targetInfo.writeUInt16LE(983, 12); Buffer.from('test-mcu').copy(targetInfo, 16); Buffer.from('boot-1').copy(targetInfo, 80);
  for (let index = 0; index < 12; index++) targetInfo[96 + index] = index + 1;
  const profiles: C5Profile[] = [1, 2].map((number) => ({ id: 'probe-' + number, name: 'C5 ' + number, host: '192.168.137.' + number,
    fingerprint: String(number).repeat(64), token: String(number).repeat(64), certificate: Buffer.from('cert-' + number) }));
  const registry = new C5Registry(profiles, {
    now: () => clock, discovery: async () => [],
    clientFactory(options: C5ClientOptions) {
      const client: Record<string, any> = { ...options, lease: 0n, pins: Buffer.alloc(48), received: 0, phase: 0, job: 0n, targetReceived: 0, targetCheckpoint: 0, targetTotal: 0, targetPhase: 0, targetJob: 0n, targetProtocol: 0, targetImage: Buffer.alloc(0), targetWriteOffsets: [], targetOverreport: false, closed: false, malformedOta: false,
        close() { this.closed = true; }, async info() { if (this.closed) throw new Error('client is closed'); if (this.fail) throw new Error('connection lost'); return { id: 'c5-00000000000' + options.id.slice(-1), board: 'esp32c5-n4', pendingBoot: Boolean(this.pendingBoot) }; },
        async acquire() { this.lease = 42n; }, async renew(ttl = 30000) { this.renewed = true; this.renewTtl = ttl; }, async release() { this.lease = 0n; },
        async rpc(op: number, data: Buffer = Buffer.alloc(0)) {
          if (op === OP.PINS) { data.copy(this.pins, (data[0] - 1) * 12); return Buffer.alloc(0); }
          if (op === OP.CONFIG) return this.pins;
          if (op === OP.BUS) { this.lastBus = Buffer.from(data); return Buffer.alloc(data.readUInt16LE(6), 0x5a); }
          if (op === OP.OTA_BEGIN) { this.job = data.readBigUInt64LE(); this.total = data.readUInt32LE(12); this.phase = 1; }
          if (op === OP.OTA_WRITE) { assert.equal(data.readUInt32LE(8), this.received); this.received += data.length - 12; }
          if (op === OP.OTA_FINISH) { assert.equal(this.received, this.total); this.phase = 2; return Buffer.alloc(0); }
          if (op === OP.OTA_ABORT) { this.job = 0n; this.received = 0; this.total = 0; this.phase = 0; return Buffer.alloc(0); }
          if (op === OP.OTA_ACTIVATE) { this.job = data.readBigUInt64LE(); this.phase = 3; return Buffer.alloc(0); }
          if (op === OP.TARGET_INFO) return targetInfo;
          if (op === OP.TARGET_OTA_BEGIN) {
            this.targetProtocol = data[0];
            const job = data.readBigUInt64LE(1), total = data.readUInt32LE(13);
            if (this.targetJob !== job) {
              this.targetJob = job; this.targetTotal = total; this.targetReceived = 0; this.targetCheckpoint = 0;
              this.targetImage = Buffer.alloc(total); this.targetPhase = 1;
            }
          }
          if (op === OP.TARGET_OTA_WRITE) {
            assert.equal(data.readBigUInt64LE(), this.targetJob);
            const offset = data.readUInt32LE(8), bytes = data.subarray(12); this.targetWriteOffsets.push(offset);
            if (offset < this.targetReceived) {
              assert.ok(offset + bytes.length <= this.targetReceived);
              assert.ok(this.targetImage.subarray(offset, offset + bytes.length).equals(bytes));
            } else {
              assert.equal(offset, this.targetReceived); bytes.copy(this.targetImage, offset);
              this.targetReceived += bytes.length; this.targetCheckpoint = this.targetReceived;
            }
          }
          if (op === OP.TARGET_OTA_FINISH) { assert.equal(data.readBigUInt64LE(), this.targetJob); assert.equal(this.targetReceived, this.targetTotal); this.targetPhase = 2; }
          if (op === OP.TARGET_OTA_ACTIVATE) { this.targetPhase = 3; }
          if (op === OP.TARGET_OTA_ABORT) { this.targetJob = 0n; this.targetReceived = 0; this.targetTotal = 0; this.targetPhase = 0; }
          if (op === OP.TARGET_OTA_STATUS || op === OP.TARGET_OTA_BEGIN || op === OP.TARGET_OTA_WRITE || op === OP.TARGET_OTA_FINISH || op === OP.TARGET_OTA_ACTIVATE || op === OP.TARGET_OTA_ABORT) {
            const state = Buffer.alloc(64); state.writeBigUInt64LE(this.targetJob, 0); state[8] = this.targetPhase;
            const reportedReceived = this.targetOverreport && op === OP.TARGET_OTA_WRITE ? this.targetReceived + 1 : this.targetReceived;
            state.writeUInt32LE(reportedReceived, 12); state.writeUInt32LE(this.targetCheckpoint, 16); state.writeUInt32LE(this.targetTotal, 20); Buffer.from('v1').copy(state, 32); return state;
          }
          if (new Set<number>([OP.OTA_BEGIN, OP.OTA_WRITE, OP.OTA_STATUS]).has(op)) {
            const state = Buffer.alloc(28); state.writeBigUInt64LE(this.job); state.writeUInt32LE(this.phase, 8);
            state.writeUInt32LE(this.malformedOta ? (this.total ?? 0) + 1 : this.received, 12);
            state.writeUInt32LE(this.total ?? 0, 20); state.writeUInt32LE(this.pendingBoot ? 1 : 0, 24); return state;
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
    assert.equal(reply.body.data, '5a5a');
    assert.deepEqual([...clients[0].lastBus.subarray(0, 10)], [2, 0, 0, 0, 2, 0, 2, 0, 50, 0]);
    assert.equal(clients[0].lastBus.subarray(12).toString('hex'), 'aabb');
    const spi = await api('/probe-1/bus', 'POST', { sessionId: session.id, protocol: 3, data: '010203', receive: 3 });
    assert.equal(spi.status, 200); assert.equal(spi.body.data, '5a5a5a');
    assert.equal(clients[0].lastBus[0], 3); assert.equal(clients[0].lastBus.readUInt16LE(4), 3); assert.equal(clients[0].lastBus.readUInt16LE(6), 3);
    assert.equal((await api('/probe-1/bus', 'POST', { sessionId: session.id, protocol: 3, data: '010203', receive: 2 })).status, 400);
    assert.equal((await api('/probe-1/bus', 'POST', { sessionId: session.id, protocol: 4, address: 1, data: '01' })).status, 400);
    assert.equal((await api('/probe-1/bus', 'POST', { sessionId: session.id, protocol: 2 })).status, 400);
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

test('C5 lease renewal extends the Gateway session by the same interval', async () => {
  const { registry, clients, advance } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    advance(5000);
    const renewed = await registry.renew('probe-1', session.id);
    assert.equal(clients[0].renewTtl, 60000);
    assert.equal(renewed.deadline, 66000);
  } finally { registry.close(); }
});

test('target OTA upload validates target capabilities and streams resumable UART blocks', async () => {
  const { registry, clients } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const manifest = Buffer.alloc(184), image = Buffer.alloc(2500, 0x5a);
    manifest.writeBigUInt64LE(77n, 0); manifest[8] = 1; manifest.writeUInt32LE(image.length, 12); Buffer.from('test-mcu').copy(manifest, 80);
    const wire = Buffer.concat([manifest, image]); const chunks = [];
    for (let offset = 0; offset < wire.length; offset += 311) chunks.push(wire.subarray(offset, offset + 311));
    const state = await uploadTarget(registry, 'probe-1', session.id, 2, Readable.from(chunks), wire.length);
    assert.equal(state.jobId, '77'); assert.equal(state.phase, 2); assert.equal(state.received, image.length);
    assert.equal(clients[0].targetProtocol, 2); assert.equal(clients[0].targetReceived, image.length);
  } finally { registry.close(); }
});

test('target OTA rejects a manifest for a different board before BEGIN', async () => {
  const { registry } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const manifest = Buffer.alloc(184), image = Buffer.alloc(8);
    manifest.writeBigUInt64LE(79n, 0); manifest[8] = 1; manifest.writeUInt32LE(image.length, 12); Buffer.from('other-mcu').copy(manifest, 80);
    await assert.rejects(uploadTarget(registry, 'probe-1', session.id, 2, Readable.from([Buffer.concat([manifest, image])]), manifest.length + image.length), { code: 'target_board_mismatch' });
  } finally { registry.close(); }
});

test('target OTA replays bytes after the durable checkpoint and resumes safely', async () => {
  const { registry, clients } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const image = Buffer.alloc(2500, 0x5a), manifest = Buffer.alloc(184);
    manifest.writeBigUInt64LE(77n); manifest[8] = 1; manifest.writeUInt32LE(image.length, 12); Buffer.from('test-mcu').copy(manifest, 80);
    const client = clients[0]; client.targetJob = 77n; client.targetTotal = image.length; client.targetReceived = 1600; client.targetCheckpoint = 1000;
    client.targetPhase = 1; client.targetImage = Buffer.from(image);
    const wire = Buffer.concat([manifest, image]), chunks = [];
    for (let offset = 0; offset < wire.length; offset += 311) chunks.push(wire.subarray(offset, offset + 311));
    const state = await uploadTarget(registry, 'probe-1', session.id, 2, Readable.from(chunks), wire.length);
    assert.equal(state.received, image.length);
    assert.equal(client.targetImage.equals(image), true);
    assert.equal(client.targetWriteOffsets[0], 1000);
  } finally { registry.close(); }
});

test('target OTA accepts an already verified job without rewriting its image', async () => {
  const { registry, clients } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const image = Buffer.alloc(64, 0x2a), manifest = Buffer.alloc(184);
    manifest.writeBigUInt64LE(81n); manifest[8] = 1; manifest.writeUInt32LE(image.length, 12); Buffer.from('test-mcu').copy(manifest, 80);
    const client = clients[0]; client.targetJob = 81n; client.targetTotal = image.length; client.targetReceived = image.length;
    client.targetCheckpoint = image.length; client.targetPhase = 2; client.targetImage = Buffer.from(image);
    const state = await uploadTarget(registry, 'probe-1', session.id, 2, Readable.from([Buffer.concat([manifest, image])]), manifest.length + image.length);
    assert.equal(state.phase, 2); assert.deepEqual(client.targetWriteOffsets, []);
  } finally { registry.close(); }
});

test('target OTA rejects a write acknowledgment beyond the requested range', async () => {
  const { registry, clients } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const manifest = Buffer.alloc(184), image = Buffer.alloc(2500, 0x5a);
    manifest.writeBigUInt64LE(82n); manifest[8] = 1; manifest.writeUInt32LE(image.length, 12); Buffer.from('test-mcu').copy(manifest, 80);
    clients[0].targetOverreport = true;
    await assert.rejects(uploadTarget(registry, 'probe-1', session.id, 2, Readable.from([Buffer.concat([manifest, image])]), manifest.length + image.length), { code: 'target_ota_state' });
  } finally { registry.close(); }
});

test('Gateway exposes target OTA upload and status routes with protocol selection', async () => {
  const { registry } = fixtures(); const server = createGatewayServer({ c5Registry: registry });
  server.listen(0, '127.0.0.1'); await once(server, 'listening'); const base = loopbackUrl(server);
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const manifest = Buffer.alloc(184), image = Buffer.alloc(64, 0x2a);
    manifest.writeBigUInt64LE(78n, 0); manifest[8] = 1; manifest.writeUInt32LE(image.length, 12); Buffer.from('test-mcu').copy(manifest, 80);
    const response = await fetch(base + '/api/v1/c5/probe-1/target-ota', { method: 'POST', headers: {
      'content-type': 'application/octet-stream', 'content-length': String(manifest.length + image.length),
      'x-tetherbench-session': session.id, 'x-tetherbench-protocol': 'uart',
    }, body: Buffer.concat([manifest, image]) });
    assert.equal(response.status, 200); assert.equal((await response.json() as { phase: number }).phase, 2);
    const status = await fetch(base + '/api/v1/c5/probe-1/target-ota/status', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ sessionId: session.id }) });
    assert.equal(status.status, 200); assert.equal((await status.json() as { total: number }).total, image.length);
  } finally { server.closeAllConnections(); await new Promise<void>((resolve) => server.close(() => resolve())); registry.close(); }
});

test('OTA upload rejects an inconsistent checkpoint from the C5', async () => {
  const { registry, clients } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const manifest = Buffer.alloc(176), image = Buffer.alloc(288);
    manifest.writeBigUInt64LE(43n); manifest.writeUInt32LE(image.length, 12); clients[0].malformedOta = true;
    await assert.rejects(uploadC5(registry, 'probe-1', session.id, Readable.from([Buffer.concat([manifest, image])]), manifest.length + image.length), { code: 'probe_transport' });
    assert.equal(registry.record('probe-1').connected, false);
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

test('OTA commands refresh cached state and maintenance reconciles a pending boot or rollback', async () => {
  const { registry, clients } = fixtures();
  try {
    await registry.refresh(); const session = await registry.acquire('probe-1');
    const record = registry.record('probe-1');
    clients[0].job = 41n; clients[0].phase = 1; clients[0].received = 4096; clients[0].total = 8192;
    await registry.maintain();
    assert.deepEqual(record.ota, { jobId: '41', phase: 1, received: 4096, checkpoint: 0, total: 8192, pendingBoot: false });

    record.ota = { jobId: '42', phase: 1, received: 4096, checkpoint: 4096, total: 8192, pendingBoot: false };
    const job = Buffer.alloc(8); job.writeBigUInt64LE(42n);
    await registry.command('probe-1', session.id, OP.OTA_ABORT, job);
    assert.deepEqual(record.ota, { jobId: '0', phase: 0, received: 0, checkpoint: 0, total: 0, pendingBoot: false });

    record.ota = { jobId: '42', phase: 2, received: 8192, checkpoint: 8192, total: 8192, pendingBoot: false };
    clients[0].received = 8192; clients[0].total = 8192;
    await registry.command('probe-1', session.id, OP.OTA_ACTIVATE, job);
    assert.equal(record.ota.phase, 3);
    clients[0].pendingBoot = true;
    await registry.maintain();
    assert.equal(record.ota?.phase, 3); assert.equal(record.ota?.pendingBoot, true);

    clients[0].pendingBoot = false; clients[0].phase = 5;
    await registry.maintain();
    assert.equal(record.ota?.phase, 5); assert.equal(record.ota?.pendingBoot, false);
  } finally { registry.close(); }
});
