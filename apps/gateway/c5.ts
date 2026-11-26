import { createSocket } from 'node:dgram';
import { createHash, randomBytes, randomUUID, X509Certificate } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { C5Client, OP, ProbeError, decodeOta } from '../../firmware/probe-c5/tools/client.ts';

export interface C5Profile {
  id: string;
  name: string;
  host?: string;
  fingerprint: string;
  token: string;
  certificate: Buffer;
}

export interface C5DeviceInfo {
  id: string;
  board: string;
  version: string;
  idf: string;
  protocol: number;
  maxPayload: number;
  dapPacket: number;
  leaseActive: boolean;
  allowedPins: number;
  freeHeap: number;
  minHeap: number;
  wifi: { ip: string; ap: boolean; channel: number; rssi: number; phy: number };
  otaAvailable: boolean;
  pendingBoot: boolean;
  bandStage: number;
  capabilities: string[];
}

export interface C5OtaState {
  jobId: string;
  phase: number;
  received: number;
  checkpoint: number;
  total: number;
  pendingBoot: boolean;
}

export interface C5Session {
  id: string;
  deviceId: string;
  deadline: number;
}

export interface C5ScanSample {
  bssid: string;
  channel: number;
  rssi: number;
  wifi6: boolean;
  authmode: number;
  sightings: number;
  age_ms: number;
}

export interface C5LinkEstimate {
  rssi: number;
  loss_permille: number;
  rtt_ms: number;
  samples: number;
  measured?: number;
}

export interface C5CandidateLink extends C5LinkEstimate {
  age_ms: number;
}

export type C5AdapterType = 'openocd' | 'usb-dap' | 'usb-uart' | 'usb-spi' | 'usb-i2c';
export interface C5AdapterEntry {
  type: C5AdapterType;
  sessionId: string;
  server: { port: number; readonly connected: boolean; close(): void } | null;
}

export interface C5DeviceRecord {
  id: string;
  profile: C5Profile;
  client: InstanceType<typeof C5Client> | null;
  info: C5DeviceInfo | null;
  connected: boolean;
  busy: boolean;
  session: C5Session | null;
  lastSeenAt: string | null;
  error: string | null;
  ota: C5OtaState | null;
  adapter: C5AdapterEntry | null;
  waiter: boolean;
  idle: Promise<void>;
  releaseIdle?: () => void;
}

export interface C5DeviceSummary {
  id: string;
  name: string;
  transport: 'c5';
  host: string | null;
  connected: boolean;
  info: C5DeviceInfo | null;
  busy: boolean;
  sessionActive: boolean;
  lastSeenAt: string | null;
  error: string | null;
  ota: C5OtaState | null;
  adapter: C5AdapterType | null;
}

export interface C5PinConfiguration {
  protocol: number;
  count: number;
  hz: number;
  pins: number[];
}

export interface C5ScanHint {
  host: string;
  port: number;
  identity: string;
  fingerprint: string;
}

export interface C5ClientOptions extends C5Profile { host: string }
export type C5ClientFactory = (options: C5ClientOptions) => InstanceType<typeof C5Client>;

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export class ControlError extends Error {
  readonly status: number;
  readonly code: string;
  constructor(status: number, code: string, message: string) { super(message); this.name = 'ControlError'; this.status = status; this.code = code; }
}
function reject(status: number, code: string, message: string): never { throw new ControlError(status, code, message); }
const median = (values: number[]): number => { const sorted = [...values].sort((a, b) => a - b); return sorted[Math.floor(sorted.length / 2)]; };
const scoreBandLink = ({ rssi, loss_permille, rtt_ms }: Pick<C5LinkEstimate, 'rssi' | 'loss_permille' | 'rtt_ms'>): number => (rssi < -90 ? 0 : rssi > -40 ? 100 : 2 * (rssi + 90)) - Math.floor(loss_permille / 10) - (rtt_ms === 0xffff ? 0 : Math.floor(rtt_ms / 5));
function decodeBandScan(data: Buffer): C5ScanSample[] {
  if (data.length < 4) reject(502, 'invalid_wifi_scan', 'C5 returned a truncated Wi-Fi scan.');
  const count = data.readUInt32LE(0);
  if (count > 16 || data.length !== 4 + count * 16) reject(502, 'invalid_wifi_scan', 'C5 returned an invalid Wi-Fi scan length.');
  return Array.from({ length: count }, (_, index) => {
    const offset = 4 + index * 16, bssid = data.subarray(offset, offset + 6).toString('hex');
    const channel = data[offset + 6], rssi = data.readInt8(offset + 7), wifi6 = data[offset + 8], authmode = data[offset + 9], sightings = data[offset + 10];
    const age_ms = data.readUInt32LE(offset + 12);
    if (bssid === '000000000000' || !((channel >= 1 && channel <= 11) || [36, 40, 44, 48].includes(channel))
      || rssi < -110 || rssi > -1 || wifi6 > 1 || authmode > 7 || sightings > 5 || !sightings || data[offset + 11] !== 0 || age_ms > 30000) {
      reject(502, 'invalid_wifi_scan', 'C5 returned an invalid access-point measurement.');
    }
    return { bssid, channel, rssi, wifi6: Boolean(wifi6), authmode, sightings, age_ms };
  });
}
function chooseBandCandidate(first: C5ScanSample[], second: C5ScanSample[], oldA: number, oldB: number): { channel: number; samples: [C5CandidateLink, C5CandidateLink]; score: number } | null {
  const byChannel = new Map<number, C5ScanSample>();
  for (const sample of second) {
    if (!sample.wifi6 || sample.authmode === 0 || sample.sightings < 4 || sample.age_ms > 30000 || sample.channel === oldA || sample.channel === oldB) continue;
    const current = byChannel.get(sample.channel);
    if (!current || sample.rssi > current.rssi) byChannel.set(sample.channel, sample);
  }
  let best: { channel: number; samples: [C5CandidateLink, C5CandidateLink]; score: number } | null = null;
  for (const a of first) {
    const b = byChannel.get(a.channel);
    if (!b || !a.wifi6 || a.sightings < 4 || a.age_ms > 30000 || a.channel === oldA || a.channel === oldB || a.authmode === 0 || b.authmode === 0) continue;
    const samples: [C5CandidateLink, C5CandidateLink] = [a, b].map((sample) => ({ rssi: sample.rssi, loss_permille: (5 - sample.sightings) * 200,
      rtt_ms: 0xffff, samples: 5, age_ms: sample.age_ms })) as [C5CandidateLink, C5CandidateLink];
    const score = Math.min(...samples.map(scoreBandLink));
    if (!best || score > best.score) best = { channel: a.channel, samples, score };
  }
  return best;
}
function bandId(id: bigint): Buffer { const payload = Buffer.alloc(8); payload.writeBigUInt64LE(id); return payload; }
function bandCommit(id: bigint, delay: number): Buffer { const payload = Buffer.alloc(12); payload.writeBigUInt64LE(id); payload.writeUInt32LE(delay, 8); return payload; }
function encodeBandPrepare(id: bigint, channel: number, currentChannel: number, links: [C5LinkEstimate, C5LinkEstimate, C5LinkEstimate, C5LinkEstimate], now: number): Buffer {
  const payload = Buffer.alloc(60); payload.writeBigUInt64LE(id); payload[8] = channel; payload[9] = currentChannel;
  for (let index = 0; index < 4; index++) {
    const link = links[index], offset = 12 + index * 12, measured = link.measured;
    if (measured === undefined) throw new ControlError(409, 'stale_measurement', 'Wi-Fi link measurement timestamp is missing.');
    const age = now - measured;
    if (age < 0 || age > 30000 || link.samples < 5 || link.rssi < -110 || link.rssi > -1
      || link.loss_permille < 0 || link.loss_permille > 1000 || link.rtt_ms < 0 || (link.rtt_ms > 5000 && link.rtt_ms !== 0xffff)) {
      reject(409, 'stale_measurement', 'Wi-Fi link measurements are invalid or older than five seconds.');
    }
    payload.writeInt16LE(link.rssi, offset); payload.writeUInt16LE(link.loss_permille, offset + 2);
    payload.writeUInt16LE(link.rtt_ms, offset + 4); payload.writeUInt16LE(link.samples, offset + 6); payload.writeUInt32LE(age, offset + 8);
  }
  return payload;
}

interface C5ProfileConfig {
  id: string;
  name: string;
  credentials: string;
  certificate: string;
  host?: string;
}

function isC5ProfileConfig(value: unknown): value is C5ProfileConfig {
  if (!value || typeof value !== 'object') return false;
  const profile = value as Record<string, unknown>;
  return typeof profile.id === 'string' && typeof profile.name === 'string'
    && typeof profile.credentials === 'string' && typeof profile.certificate === 'string'
    && (profile.host === undefined || typeof profile.host === 'string');
}

interface C5Credentials { token: string; certificate_sha256: string }

function isC5Credentials(value: unknown): value is C5Credentials {
  if (!value || typeof value !== 'object') return false;
  const credentials = value as Record<string, unknown>;
  return typeof credentials.token === 'string' && /^[a-fA-F0-9]{64}$/.test(credentials.token)
    && typeof credentials.certificate_sha256 === 'string'
    && /^[a-fA-F0-9]{64}$/.test(credentials.certificate_sha256);
}

export function verifyC5CertificateFingerprint(certificate: Uint8Array, expected: string): string {
  const fingerprint = createHash('sha256').update(certificate).digest('hex');
  if (fingerprint !== expected.toLowerCase()) {
    throw new Error('C5 certificate does not match the provisioned certificate identity.');
  }
  return fingerprint;
}

export async function loadC5Profiles(path?: string | URL): Promise<C5Profile[]> {
  const defaultPath = new URL('../../firmware/probe-c5/.local/gateway.json', import.meta.url);
  const explicit = path !== undefined;
  path ??= defaultPath;
  let profiles: unknown;
  try { profiles = JSON.parse(await readFile(path, 'utf8')); }
  catch (error) {
    if (!(error instanceof Error) || !('code' in error) || error.code !== 'ENOENT' || explicit) throw error;
    const device = new URL('../../firmware/probe-c5/.local/device.json', import.meta.url);
    try { await readFile(device); } catch (missing) { if (missing instanceof Error && 'code' in missing && missing.code === 'ENOENT') return []; throw missing; }
    profiles = [{ id: 'local-c5', name: '本机 C5', credentials: 'device.json', certificate: 'server.crt' }];
  }
  if (!Array.isArray(profiles) || profiles.length > 16) throw new Error('C5 profiles must contain at most 16 devices.');
  const folder = dirname(path instanceof URL ? fileURLToPath(path) : resolve(path));
  const ids = new Set<string>();
  return Promise.all(profiles.map(async (profile) => {
    if (!isC5ProfileConfig(profile) || !/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$/.test(profile.id) || ids.has(profile.id)
      || !profile.name.trim() || profile.name.length > 64 || (profile.host !== undefined && profile.host.length > 253)) {
      throw new Error('Invalid or duplicate C5 profile.');
    }
    ids.add(profile.id);
    const credentials: unknown = JSON.parse(await readFile(resolve(folder, profile.credentials), 'utf8'));
    if (!isC5Credentials(credentials)) {
      throw new Error('C5 credentials contain an invalid token or certificate identity.');
    }
    const certificate = new X509Certificate(await readFile(resolve(folder, profile.certificate))).raw;
    const fingerprint = verifyC5CertificateFingerprint(certificate, credentials.certificate_sha256);
    return { id: profile.id, name: profile.name.trim(), host: profile.host,
      fingerprint, token: credentials.token, certificate };
  }));
}

export function discoverC5({ broadcast = '192.168.137.255', timeout = 1500 }: { broadcast?: string; timeout?: number } = {}): Promise<C5ScanHint[]> {
  return new Promise<C5ScanHint[]>((resolvePromise, rejectPromise) => {
    const socket = createSocket('udp4'), nonce = randomBytes(8), found = new Map<string, C5ScanHint>();
    let timer: ReturnType<typeof setTimeout> | undefined, complete = false;
    const finish = (error?: Error) => {
      if (complete) return; complete = true; clearTimeout(timer); socket.close();
      if (error) rejectPromise(error); else resolvePromise([...found.values()]);
    };
    socket.once('error', finish);
    socket.on('message', (data, peer) => {
      if (data.length !== 124 || data.toString('ascii', 0, 8) !== 'TBC5HERE'
        || !data.subarray(8, 16).equals(nonce) || data[42] !== 1 || found.size >= 64) return;
      const id = data.subarray(16, 40).toString('ascii').split(String.fromCharCode(0))[0];
      if (!/^c5-[a-f0-9]{12}$/.test(id)) return;
      found.set(peer.address, { host: peer.address, port: data.readUInt16LE(40), identity: id,
        fingerprint: data.subarray(44, 76).toString('hex') });
    });
    socket.bind(0, '0.0.0.0', () => {
      socket.setBroadcast(true);
      timer = setTimeout(() => finish(), timeout); timer.unref();
      socket.send(Buffer.concat([Buffer.from('TBC5DISC'), nonce]), 4440, broadcast, (error) => { if (error) finish(error); });
    });
  });
}

export class C5Registry {
  readonly records: Map<string, C5DeviceRecord>;
  readonly clientFactory: C5ClientFactory;
  readonly discovery: () => Promise<C5ScanHint[]>;
  readonly now: () => number;
  readonly sleep: (ms: number) => Promise<void>;
  readonly timer: ReturnType<typeof setInterval>;
  closed: boolean;
  lastDiscovery: number;
  discoveryError: string | null = null;
  refreshing: Promise<C5DeviceSummary[]> | null = null;

  constructor(profiles: C5Profile[] = [], { clientFactory = (options: C5ClientOptions) => new C5Client(options), discovery = discoverC5, now = Date.now, sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms)) }: {
    clientFactory?: C5ClientFactory;
    discovery?: () => Promise<C5ScanHint[]>;
    now?: () => number;
    sleep?: (ms: number) => Promise<void>;
  } = {}) {
    this.records = new Map<string, C5DeviceRecord>(profiles.map((profile) => [profile.id, { id: profile.id, profile, client: null, info: null,
      connected: false, busy: false, session: null, lastSeenAt: null, error: null, ota: null, adapter: null,
      waiter: false, idle: Promise.resolve() }]));
    this.clientFactory = clientFactory; this.discovery = discovery; this.now = now; this.sleep = sleep; this.closed = false;
    this.lastDiscovery = -Infinity;
    this.timer = setInterval(() => this.maintain(), 5000); this.timer.unref();
  }
  record(id: string): C5DeviceRecord { const record = this.records.get(id); if (!record) throw new ControlError(404, 'device_not_found', 'C5 device is not configured.'); return record; }
  client(record: C5DeviceRecord): InstanceType<typeof C5Client> {
    if (!record.client) throw new ControlError(503, 'device_offline', 'C5 is offline.');
    return record.client;
  }
  list(): C5DeviceSummary[] {
    return [...this.records].map(([id, record]) => ({ id, name: record.profile.name, transport: 'c5',
      host: record.client?.host ?? record.profile.host ?? null, connected: record.connected,
      info: record.info, busy: record.busy, sessionActive: Boolean(record.session), lastSeenAt: record.lastSeenAt,
      error: record.error, ota: record.ota, adapter: record.adapter?.type ?? null }));
  }
  async run<T>(id: string, work: (record: C5DeviceRecord) => Promise<T>, { offline = false, wait = false }: { offline?: boolean; wait?: boolean } = {}): Promise<T> {
    if (this.closed) reject(503, 'gateway_stopping', 'Gateway is stopping.');
    const record = this.record(id);
    if (wait && record.busy && !record.waiter) {
      record.waiter = true; let timer: ReturnType<typeof setTimeout> | undefined;
      try {
        await Promise.race([record.idle, new Promise<void>((_, fail) => { timer = setTimeout(() => fail(new ControlError(409, 'device_busy', 'C5 operation did not finish within six seconds.')), 6000); timer.unref(); })]);
      } finally { record.waiter = false; clearTimeout(timer); }
    }
    if (this.closed) reject(503, 'gateway_stopping', 'Gateway is stopping.');
    if (record.busy) reject(409, 'device_busy', 'C5 already has an operation in progress.');
    if (!offline && (!record.connected || !record.client)) reject(503, 'device_offline', 'C5 is offline.');
    record.busy = true;
    let releaseIdle!: () => void; record.idle = new Promise<void>((resolve) => { releaseIdle = resolve; });
    try { return await work(record); }
    catch (error) {
      if (error instanceof ProbeError) {
        if (error.status === 4) { record.session = null; if (record.client) record.client.lease = 0n; }
        throw new ControlError(error.status === 3 || error.status === 4 || error.status === 8 || error.status === 9 ? 409 : 422,
          'probe_' + error.status, error.message);
      }
      if (!(error instanceof ControlError)) {
        record.connected = false; record.error = errorMessage(error); record.client?.close();
        throw new ControlError(502, 'probe_transport', errorMessage(error));
      }
      throw error;
    } finally { record.busy = false; releaseIdle(); }
  }
  async runMany<T>(ids: [string, string], work: (records: [C5DeviceRecord, C5DeviceRecord]) => Promise<T>): Promise<T> {
    if (this.closed) reject(503, 'gateway_stopping', 'Gateway is stopping.');
      const records: [C5DeviceRecord, C5DeviceRecord] = [this.record(ids[0]), this.record(ids[1])];
    if (new Set(records).size !== records.length) reject(400, 'duplicate_device', 'Choose two different C5 devices.');
    if (records.some((record) => record.busy)) reject(409, 'device_busy', 'A C5 operation is already in progress.');
    if (records.some((record) => !record.connected || !record.client)) reject(503, 'device_offline', 'Both C5 devices must be connected.');
    for (const record of records) {
      record.busy = true; let releaseIdle!: () => void; record.idle = new Promise<void>((resolve) => { releaseIdle = resolve; }); record.releaseIdle = releaseIdle;
    }
    try { return await work(records as [C5DeviceRecord, C5DeviceRecord]); }
    finally { for (const record of records) { record.busy = false; record.releaseIdle?.(); delete record.releaseIdle; } }
  }
  async probe<T>(record: C5DeviceRecord, work: () => Promise<T>): Promise<T> {
    try { return await work(); }
    catch (error) {
      if (error instanceof ProbeError) {
        if (error.status === 4) { record.session = null; if (record.client) record.client.lease = 0n; }
        throw new ControlError(error.status === 3 || error.status === 4 || error.status === 8 || error.status === 9 ? 409 : 422,
          'probe_' + error.status, error.message);
      }
      if (!(error instanceof ControlError)) {
        record.connected = false; record.error = errorMessage(error); record.client?.close();
        throw new ControlError(502, 'probe_transport', errorMessage(error));
      }
      throw error;
    }
  }
  async negotiateBand(id: string, sessionId: string, peerId: string, peerSessionId: string): Promise<{ transactionId: string; channel: number; previousChannels: number[]; score: { current: number; candidate: number }; samples: number }> {
    if (typeof peerId !== 'string' || typeof peerSessionId !== 'string' || id === peerId) reject(400, 'invalid_peer', 'Choose a different paired C5 and provide both session IDs.');
    return this.runMany([id, peerId], async ([a, b]) => {
      this.owner(a, sessionId); this.owner(b, peerSessionId);
      for (const record of [a, b]) {
        if (record.adapter) reject(409, 'adapter_active', 'Stop both active adapters before changing Wi-Fi channels.');
        if (!record.info || record.info.wifi?.ap || record.info.wifi?.phy !== 5 || record.info.bandStage !== 0) reject(409, 'wifi6_sta_required', 'Both devices must be idle Wi-Fi 6 HE20 stations.');
        if (record.ota?.phase === 1 || record.ota?.phase === 3) reject(409, 'ota_active', 'Finish or abort both OTA jobs before changing Wi-Fi channels.');
      }
      const infoA = a.info, infoB = b.info;
      if (!infoA || !infoB) reject(409, 'wifi_unavailable', 'Both devices need current device information.');
      const oldInfo: [C5DeviceInfo, C5DeviceInfo] = [infoA, infoB];
      if (oldInfo.some((info) => !Number.isInteger(info.wifi.channel) || info.wifi.channel < 1)) reject(409, 'wifi_unavailable', 'Both devices need a current Wi-Fi channel.');
      await Promise.all([a, b].map((record) => this.renewBandLease(record)));
      for (const record of [a, b]) {
        const pins = await this.probe(record, () => this.client(record).rpc(OP.CONFIG));
        if (pins.length !== 48 || [0, 1, 2, 3].some((protocol) => pins[protocol * 12 + 1] !== 0)) reject(409, 'device_resources_busy', 'Release SWD and all bus pins on both devices before changing channels.');
      }
      const scans = await Promise.all([a, b].map((record) => this.scanForBand(record)));
      const candidate = chooseBandCandidate(scans[0], scans[1], oldInfo[0].wifi.channel, oldInfo[1].wifi.channel);
      if (!candidate) reject(409, 'no_common_candidate', 'No stable Wi-Fi 6 access point on a better common channel was found.');
      const current = await Promise.all([a, b].map((record) => this.measureCurrentLink(record)));
      const transactionId = randomBytes(8).readBigUInt64LE() || 1n;
      const links = current.map((sample) => ({ ...sample, measured: this.now() }));
      const next: [C5LinkEstimate, C5LinkEstimate] = [
        { ...candidate.samples[0], measured: this.now() - candidate.samples[0].age_ms },
        { ...candidate.samples[1], measured: this.now() - candidate.samples[1].age_ms },
      ];
      const currentScore = Math.min(...links.map((sample) => scoreBandLink(sample)));
      const candidateScore = Math.min(...next.map((sample) => scoreBandLink(sample)));
      if (candidateScore < currentScore + 10) reject(409, 'candidate_not_better', 'The measured candidate does not improve both current links by at least 10 points.');
      const ordered: [
        [C5LinkEstimate, C5LinkEstimate, C5LinkEstimate, C5LinkEstimate],
        [C5LinkEstimate, C5LinkEstimate, C5LinkEstimate, C5LinkEstimate],
      ] = [[links[0], links[1], next[0], next[1]], [links[1], links[0], next[1], next[0]]];
      const confirmed: C5DeviceRecord[] = [];
      const canceled = new Set<C5DeviceRecord>();
      const payloadFor = (index: 0 | 1) => encodeBandPrepare(transactionId, candidate.channel, oldInfo[index].wifi.channel, ordered[index], this.now());
      const cancel = async () => {
        const deadline = Date.now() + 10000; let discoveryAt = Date.now();
        while (canceled.size < 2 && Date.now() < deadline) {
          if (Date.now() - discoveryAt >= 1000) {
            discoveryAt = Date.now();
            const hints = await this.discovery().catch(() => []);
            for (const record of [a, b]) {
              const hint = hints.find((item) => item.identity === record.info?.id && item.fingerprint === record.profile.fingerprint && item.port === 443);
              if (hint && this.client(record).host !== hint.host) this.replaceClient(record, hint.host);
            }
          }
          for (const [index, record] of [a, b].entries()) {
            if (canceled.has(record)) continue;
            try { await this.client(record).rpc(OP.BAND_CANCEL, bandId(transactionId)); canceled.add(record); }
            catch (error) {
              if (error instanceof ProbeError && [1, 4].includes(error.status)) {
                try {
                  const info = await this.client(record).info(); record.info = info;
                  if (info.bandStage === 0 && info.wifi?.channel === oldInfo[index].wifi.channel) canceled.add(record);
                } catch (readError) {
                  if (!(readError instanceof ProbeError)) this.replaceClient(record, record.client?.host ?? record.profile.host);
                }
              }
              else if (!(error instanceof ProbeError)) this.replaceClient(record, record.client?.host ?? record.profile.host);
            }
          }
          if (canceled.size < 2) await this.sleep(150);
        }
        return canceled.size === 2;
      };
      try {
        const prep = await Promise.allSettled(([a, b] as const).map((record, index) => this.probe(record, () => this.client(record).rpc(OP.BAND_PREPARE, payloadFor(index as 0 | 1)))));
        const rejectedPrep = prep.find((result) => result.status === 'rejected');
        if (rejectedPrep) throw rejectedPrep.reason;
        const commits = await Promise.allSettled([a, b].map((record) => this.probe(record, () => this.client(record).rpc(OP.BAND_COMMIT, bandCommit(transactionId, 3000)))));
        const rejectedCommit = commits.find((result) => result.status === 'rejected');
        if (rejectedCommit) throw rejectedCommit.reason;
        const deadline = Date.now() + 12000;
        let discoveryAt = Date.now();
        while (confirmed.length < 2 && Date.now() < deadline) {
          if (Date.now() - discoveryAt >= 1000) {
            discoveryAt = Date.now();
            const hints = await this.discovery().catch(() => []);
            for (const record of [a, b]) {
              const identity = record.info?.id;
              const hint = hints.find((item) => item.identity === identity && item.fingerprint === record.profile.fingerprint && item.port === 443);
              if (hint && this.client(record).host !== hint.host) this.replaceClient(record, hint.host);
            }
          }
          for (const record of [a, b]) {
            if (confirmed.includes(record)) continue;
            try {
              const info = await this.client(record).info(); record.info = info;
              if (info.wifi?.channel === candidate.channel && info.wifi?.phy === 5 && info.bandStage === 3) {
                await this.probe(record, () => this.client(record).rpc(OP.BAND_CONFIRM, bandId(transactionId))); confirmed.push(record);
              }
            } catch (error) {
              if (error instanceof ProbeError || error instanceof ControlError) throw error;
              this.replaceClient(record, record.client?.host ?? record.profile.host);
            }
          }
          if (confirmed.length < 2) await this.sleep(150);
        }
        if (confirmed.length !== 2) throw new ControlError(504, 'band_confirm_timeout', 'Both C5 devices did not confirm the new channel in time.');
        const finalized = await Promise.allSettled([a, b].map((record) => this.probe(record, () => this.client(record).rpc(OP.BAND_FINALIZE, bandId(transactionId)))));
        const rejectedFinalize = finalized.find((result) => result.status === 'rejected');
        if (rejectedFinalize) throw rejectedFinalize.reason;
        const infos = await Promise.all([a, b].map((record) => this.probe(record, () => this.client(record).info())));
        if (infos.some((info) => info.wifi?.channel !== candidate.channel || info.wifi?.phy !== 5 || info.bandStage !== 0)) {
          throw new ControlError(502, 'band_finalize_unverified', 'The devices did not both report an idle, verified Wi-Fi 6 link after finalization.');
        }
        for (const [index, record] of [a, b].entries()) record.info = infos[index];
        return { transactionId: transactionId.toString(), channel: candidate.channel, previousChannels: oldInfo.map((info) => info.wifi.channel),
          score: { current: currentScore, candidate: candidateScore }, samples: 5 };
      } catch (error) {
        const compensated = await cancel();
        if (compensated !== true) throw new ControlError(502, 'band_rollback_unconfirmed', 'The operation failed and the Gateway could not verify both devices restored their previous channels; C5 will automatically roll back an unconfirmed switch.');
        throw error;
      }
    });
  }
  async scanForBand(record: C5DeviceRecord): Promise<C5ScanSample[]> {
    const deadline = Date.now() + 30000; let renewAt = Date.now() + 10000;
    while (Date.now() < deadline) {
      try {
        const data = await this.probe(record, () => this.client(record).rpc(OP.WIFI_SCAN));
        return decodeBandScan(data);
      } catch (error) {
        if (!(error instanceof ControlError) || error.code !== 'probe_3') throw error;
        if (Date.now() >= renewAt) { await this.renewBandLease(record); renewAt = Date.now() + 10000; }
        await this.sleep(100);
      }
    }
    return reject(504, 'wifi_scan_timeout', 'Five-sample Wi-Fi scan did not finish within 30 seconds.');
  }
  async measureCurrentLink(record: C5DeviceRecord): Promise<C5LinkEstimate> {
    const values = [], times = []; let lost = 0, renewAt = Date.now() + 10000;
    for (let index = 0; index < 5; index++) {
      if (Date.now() >= renewAt) { await this.renewBandLease(record); renewAt = Date.now() + 10000; }
      const started = performance.now();
      try {
        const info = await this.probe(record, () => this.client(record).info());
        const elapsed = Math.round(performance.now() - started);
        if (info.wifi?.ap || info.wifi?.phy !== 5 || !Number.isInteger(info.wifi.rssi) || info.wifi.rssi < -110 || info.wifi.rssi > -1 || elapsed > 5000) {
          reject(409, 'wifi6_measurement_invalid', 'Current-link samples must come from a live Wi-Fi 6 HE20 station.');
        }
        values.push({ rssi: info.wifi.rssi, rtt_ms: elapsed }); record.info = info;
      } catch (error) {
        if (!(error instanceof ControlError) || error.code !== 'probe_transport') throw error;
        lost++;
        this.replaceClient(record, record.client?.host ?? record.profile.host);
      }
      times.push(this.now());
    }
      const firstMeasured = times[0], lastMeasured = times.at(-1);
      if (firstMeasured === undefined || lastMeasured === undefined || lastMeasured - firstMeasured > 5000) reject(409, 'stale_measurement', 'Current-link measurements exceeded the five-second freshness window.');
    if (values.length < 4) reject(409, 'wifi_link_unstable', 'Fewer than four of five current-link probes succeeded.');
    return { rssi: median(values.map((sample) => sample.rssi)), loss_permille: Math.ceil(lost * 1000 / 5),
      rtt_ms: median(values.map((sample) => sample.rtt_ms)), samples: 5, measured: times.at(-1) };
  }
  replaceClient(record: C5DeviceRecord, host: string | undefined): void {
    if (!host) reject(503, 'device_offline', 'C5 has no discovered host.');
    const lease = record.client?.lease ?? 0n; record.client?.close();
    const client = this.clientFactory({ ...record.profile, host }); client.lease = lease; record.client = client;
    record.connected = true; record.error = null;
  }
  async renewBandLease(record: C5DeviceRecord): Promise<void> {
    await this.probe(record, () => this.client(record).renew(60000));
    const session = record.session;
    if (!session) reject(409, 'session_expired', 'C5 session is missing or expired.');
    session.deadline = this.now() + 60000;
  }
  async refresh(): Promise<C5DeviceSummary[]> {
    if (this.closed) return this.list();
    if (this.refreshing) return this.refreshing;
    this.refreshing = (async () => {
      this.discoveryError = null;
      this.lastDiscovery = this.now();
      const hints = this.records.size ? await this.discovery().catch((error: unknown) => { this.discoveryError = errorMessage(error); return []; }) : [];
      if (this.closed) return this.list();
      await Promise.allSettled([...this.records].map(async ([id, record]) => {
        if (record.busy) return;
        const candidates = [record.client?.host ?? record.profile.host, ...hints
          .filter((hint) => hint.fingerprint === record.profile.fingerprint && hint.port === 443).map((hint) => hint.host)]
          .filter((host, index, all): host is string => typeof host === 'string' && all.indexOf(host) === index).slice(0, 3);
        if (!candidates.length) { record.connected = false; record.error = '未发现匹配设备；请确认热点以 Wi-Fi 6 工作。'; return; }
        await this.run(id, async (item) => {
          let lastError;
          for (const host of candidates) {
            let client = item.client;
            if (!client || client.closed || client.host !== host) {
              const lease = client?.lease ?? 0n; client?.close();
              client = this.clientFactory({ ...item.profile, host }); client.lease = lease; item.client = client;
            }
            try {
              item.info = await client.info();
              const info = item.info;
              if (info && [...this.records.values()].some((other) => other !== item && other.connected && other.info?.id === info.id)) {
                throw new ControlError(409, 'duplicate_device', 'Another profile already owns this physical C5.');
              }
              item.connected = true; item.error = null;
              item.lastSeenAt = new Date(this.now()).toISOString(); return;
            } catch (error) { client.close(); item.connected = false; item.error = errorMessage(error); lastError = error; }
          }
          if (lastError !== undefined) throw lastError;
          reject(503, 'device_offline', 'No matching C5 endpoint was available.');
        }, { offline: true });
      }));
      return this.list();
    })().finally(() => { this.refreshing = null; });
    return this.refreshing;
  }
  owner(record: C5DeviceRecord, sessionId: string): C5Session {
    const session = record.session;
    if (!session || session.id !== sessionId || session.deadline <= this.now()) {
      throw new ControlError(409, 'session_expired', 'C5 session is missing or expired.');
    }
    return session;
  }
  async acquire(id: string): Promise<C5Session> {
    return this.run(id, async (record) => {
      if (record.session) reject(409, 'session_busy', 'C5 already has an active session.');
      await this.client(record).acquire();
      record.session = { id: randomUUID(), deviceId: id, deadline: this.now() + 60000 };
      return { ...record.session };
    });
  }
  session(id: string, sessionId: string): C5Session { const record = this.record(id); return { ...this.owner(record, sessionId) }; }
  async renew(id: string, sessionId: string): Promise<C5Session> {
    return this.run(id, async (record) => {
      const session = this.owner(record, sessionId); await this.client(record).renew();
      session.deadline = this.now() + 60000; return { ...session };
    });
  }
  async release(id: string, sessionId: string): Promise<{ released: true }> {
    return this.run(id, async (record) => {
      this.owner(record, sessionId); await this.client(record).release(); record.session = null; return { released: true };
    });
  }
  async command(id: string, sessionId: string, op: number, payload: Buffer = Buffer.alloc(0), { adapter = null, wait = false }: { adapter?: C5AdapterEntry | null; wait?: boolean } = {}): Promise<Buffer> {
    return this.run(id, async (record) => {
      this.owner(record, sessionId);
      if (record.adapter && record.adapter !== adapter && !new Set<number>([OP.OTA_STATUS, OP.CONFIG, OP.WIFI_SCAN]).has(op)) {
        reject(409, 'adapter_active', 'Stop the active adapter before changing device resources.');
      }
      const data = await this.client(record).rpc(op, payload);
      record.lastSeenAt = new Date(this.now()).toISOString();
      if (op === OP.OTA_STATUS) record.ota = decodeOta(data);
      return data;
    }, { wait });
  }
  async status(id: string): Promise<{ info: C5DeviceInfo; ota: C5OtaState; pins: C5PinConfiguration[] }> {
    return this.run(id, async (record) => {
      const client = this.client(record);
      record.info = await client.info();
      record.ota = decodeOta(await client.rpc(OP.OTA_STATUS, Buffer.alloc(0), 0n));
      const data = await client.rpc(OP.CONFIG, Buffer.alloc(0), 0n);
      if (data.length !== 48) throw new Error('Invalid C5 pin configuration.');
      const pins: C5PinConfiguration[] = [];
      for (let offset = 0; offset < 48; offset += 12) pins.push({ protocol: data[offset], count: data[offset + 1],
        hz: data.readUInt32LE(offset + 4), pins: [0, 1, 2, 3].map((index) => data.readInt8(offset + 8 + index)) });
      record.lastSeenAt = new Date(this.now()).toISOString();
      return { info: record.info, ota: record.ota, pins };
    });
  }
  async maintain(): Promise<void> {
    if (this.closed) return;
    await Promise.allSettled([...this.records].map(async ([id, record]) => {
      if (record.busy) return;
      if (!record.connected) {
        if (record.session && record.session.deadline <= this.now()) { record.session = null; if (record.client) record.client.lease = 0n; }
        return;
      }
      await this.run(id, async (item) => {
        const client = this.client(item);
        if (item.session) {
          if (item.session.deadline <= this.now()) {
            try { await client.release(); } finally { item.session = null; }
          } else await client.renew();
        }
        item.info = await client.info();
        item.lastSeenAt = new Date(this.now()).toISOString();
      });
    }));
    if (!this.closed && this.now() - this.lastDiscovery >= 15000 && [...this.records.values()].some((record) => !record.connected)) await this.refresh();
  }
  close(): void { this.closed = true; clearInterval(this.timer); for (const record of this.records.values()) record.client?.close(); }
}
