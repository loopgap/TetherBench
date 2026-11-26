import { Agent, request } from 'node:https';
import type { RequestOptions } from 'node:https';
import { connect } from 'node:tls';
import type { ConnectionOptions, TLSSocket } from 'node:tls';
import type { Duplex } from 'node:stream';
import { randomBytes, timingSafeEqual } from 'node:crypto';

export const OP = Object.freeze({ INFO: 1, ACQUIRE: 2, RENEW: 3, RELEASE: 4, PINS: 5, CONFIG: 6,
  DAP: 16, BUS: 17, OTA_BEGIN: 32, OTA_WRITE: 33, OTA_FINISH: 34, OTA_ACTIVATE: 35,
  OTA_STATUS: 36, OTA_ABORT: 37, BOOT_CONFIRM: 38, WIFI_SCAN: 48,
  BAND_PREPARE: 49, BAND_COMMIT: 50, BAND_CONFIRM: 51, BAND_CANCEL: 52, BAND_FINALIZE: 53, REBOOT: 63 });
export const MAX_PAYLOAD = 4096;
const STATUS = ['OK', 'INVALID', 'UNAUTHORIZED', 'BUSY', 'LEASE', 'UNSUPPORTED', 'IO',
  'INTEGRITY', 'STATE', 'CONFLICT', 'TIMEOUT'];

export interface C5ClientOptions {
  host: string;
  port?: number;
  token: string;
  certificate: Buffer;
  timeout?: number;
  tlsConnect?: (options: ConnectionOptions) => TLSSocket;
}

export interface C5DeviceInfo {
  id: string; board: string; version: string; idf: string; protocol: number;
  maxPayload: number; dapPacket: number; leaseActive: boolean; allowedPins: number;
  freeHeap: number; minHeap: number;
  wifi: { ip: string; ap: boolean; channel: number; rssi: number; phy: number };
  otaAvailable: boolean; pendingBoot: boolean; bandStage: number; capabilities: string[];
}

export interface C5OtaState {
  jobId: string; phase: number; received: number; checkpoint: number; total: number; pendingBoot: boolean;
}

export class ProbeError extends Error {
  readonly code = 'probe_error';
  readonly status: number;
  readonly data: Buffer;
  constructor(status: number, data: Buffer = Buffer.alloc(0)) {
    super(`C5: ${STATUS[status] ?? 'unknown status'}`);
    this.name = 'ProbeError'; this.status = status; this.data = data;
  }
}
export function encodeFrame(op: number, id: number, lease: bigint, payload: Buffer = Buffer.alloc(0), status = 0): Buffer {
  if (!Buffer.isBuffer(payload) || payload.length > MAX_PAYLOAD) throw new RangeError('C5 payload exceeds 4096 bytes.');
  const frame = Buffer.alloc(24 + payload.length);
  frame.write('TBC5'); frame[4] = 1; frame[5] = op; frame.writeUInt16LE(status, 6);
  frame.writeUInt32LE(id, 8); frame.writeUInt32LE(payload.length, 12); frame.writeBigUInt64LE(lease, 16);
  payload.copy(frame, 24); return frame;
}
export function decodeResponse(frame: Buffer, op: number, id: number, lease: bigint): Buffer {
  if (frame.length < 24 || frame.toString('ascii', 0, 4) !== 'TBC5' || frame[4] !== 1
    || frame[5] !== op || frame.readUInt32LE(8) !== id || frame.readBigUInt64LE(16) !== lease
    || frame.readUInt32LE(12) > MAX_PAYLOAD || frame.length !== 24 + frame.readUInt32LE(12)
    || (frame.readUInt16LE(6) & 0xff00) !== 0x8000) throw new Error('Invalid C5 response frame.');
  const status = frame.readUInt16LE(6) & 255;
  if (status) throw new ProbeError(status, frame.subarray(24));
  return frame.subarray(24);
}
export function decodeOta(data: Buffer): C5OtaState {
  if (data.length !== 28) throw new Error('Invalid OTA state.');
  return { jobId: data.readBigUInt64LE().toString(), phase: data.readUInt32LE(8),
    received: data.readUInt32LE(12), checkpoint: data.readUInt32LE(16),
    total: data.readUInt32LE(20), pendingBoot: Boolean(data.readUInt32LE(24)) };
}
export class C5Client {
  readonly host: string;
  readonly port: number;
  readonly token: string;
  readonly certificate: Buffer;
  readonly timeout: number;
  lease: bigint;
  id: number;
  busy: boolean;
  readonly handshakes: Set<TLSSocket>;
  closed: boolean;
  readonly agent: Agent;

  constructor({ host, port = 443, token, certificate, timeout = 5000, tlsConnect = connect }: C5ClientOptions) {
    if (!/^[a-fA-F0-9]{64}$/.test(token) || !Buffer.isBuffer(certificate) || !certificate.length) {
      throw new TypeError('A provisioned token and DER certificate are required.');
    }
    this.host = host; this.port = port; this.token = token; this.certificate = Buffer.from(certificate); this.timeout = timeout;
    this.lease = 0n; this.id = randomBytes(4).readUInt32LE(); this.busy = false;
    this.handshakes = new Set(); this.closed = false;
    this.agent = new Agent({ keepAlive: true, maxSockets: 1, maxFreeSockets: 1 });
    this.agent.createConnection = (options: RequestOptions, callback?: (error: Error | null, stream: Duplex) => void) => {
      let finished = false;
      const socket = tlsConnect({ host: this.host, port: this.port, rejectUnauthorized: false, minVersion: 'TLSv1.2' });
      this.handshakes.add(socket);
      const timer = setTimeout(() => finish(new Error('C5 TLS handshake timed out.')), this.timeout); timer.unref();
      const finish = (error: Error | null) => {
        if (finished) return; finished = true; clearTimeout(timer); this.handshakes.delete(socket); socket.setTimeout(0);
        if (error) socket.destroy();
        callback?.(error, socket);
      };
      socket.once('error', finish);
      socket.once('close', () => { if (!finished) finish(new Error('C5 closed during TLS handshake.')); });
      socket.once('secureConnect', () => {
        const actual = socket.getPeerCertificate().raw;
        if (!actual || actual.length !== this.certificate.length || !timingSafeEqual(actual, this.certificate)) {
          finish(new Error('C5 certificate does not match provisioned identity.')); return;
        }
        finish(null);
      });
      // Only hand the socket to HTTPS after identity validation.
    };
  }
  close() { if (this.closed) return; this.closed = true; for (const socket of this.handshakes) socket.destroy(); this.agent.destroy(); }
  async rpc(op: number, payload: Buffer = Buffer.alloc(0), lease: bigint = this.lease): Promise<Buffer> {
    if (this.closed) throw new Error('C5 client is closed.');
    if (this.busy) throw new ProbeError(3);
    this.busy = true;
    try {
      this.id = (this.id + 1) >>> 0;
      const id = this.id, frame = encodeFrame(op, id, lease, payload);
      return await new Promise((resolve, reject) => {
        const outgoing = request({ hostname: this.host, port: this.port, path: '/v1/rpc', method: 'POST',
          agent: this.agent, signal: AbortSignal.timeout(this.timeout), headers: {
            Authorization: 'Bearer ' + this.token, 'Content-Type': 'application/octet-stream', 'Content-Length': frame.length,
          } }, (response) => {
          const chunks: Buffer[] = []; let size = 0;
          response.on('data', (chunk: Buffer) => {
            size += chunk.length;
            if (size > MAX_PAYLOAD + 24) { response.destroy(new Error('C5 response is too large.')); return; }
            chunks.push(chunk);
          });
          response.once('error', reject);
          response.once('end', () => {
            try {
              if (response.statusCode !== 200) throw new Error(`C5 HTTP status ${response.statusCode}.`);
              if (response.headers['content-type']?.split(';')[0] !== 'application/octet-stream') throw new Error('Invalid C5 content type.');
              resolve(decodeResponse(Buffer.concat(chunks), op, id, lease));
            } catch (error) { reject(error); }
          });
        });
        outgoing.once('error', (error: Error) => reject(error)); outgoing.end(frame);
      });
    } finally { this.busy = false; }
  }
  async info(): Promise<C5DeviceInfo> {
    const value: unknown = JSON.parse((await this.rpc(OP.INFO, Buffer.alloc(0), 0n)).toString('utf8'));
    if (!isC5DeviceInfo(value)) throw new Error('Invalid C5 information response.');
    if (value.board !== 'esp32c5-n4' || value.protocol !== 1 || !/^c5-[a-f0-9]{12}$/.test(value.id)
      || value.maxPayload !== MAX_PAYLOAD || value.dapPacket !== 1024) throw new Error('Unsupported C5 identity or protocol.');
    return value;
  }
  async acquire(ttl = 30000): Promise<void> {
    const payload = Buffer.alloc(4); payload.writeUInt32LE(ttl);
    const data = await this.rpc(OP.ACQUIRE, payload, 0n);
    if (data.length !== 12 || !data.readBigUInt64LE() || data.readUInt32LE(8) !== ttl) throw new Error('Invalid lease grant.');
    this.lease = data.readBigUInt64LE();
  }
  async renew(ttl = 30000): Promise<void> { const data = Buffer.alloc(4); data.writeUInt32LE(ttl); await this.rpc(OP.RENEW, data); }
  async release(): Promise<void> { if (this.lease) { await this.rpc(OP.RELEASE); this.lease = 0n; } }
}

function isC5DeviceInfo(value: unknown): value is C5DeviceInfo {
  if (!value || typeof value !== 'object') return false;
  const info = value as Record<string, unknown>;
  const wifi = info.wifi;
  if (wifi === null || typeof wifi !== 'object') return false;
  const wifiInfo = wifi as Record<string, unknown>;
  return typeof info.id === 'string' && typeof info.board === 'string' && typeof info.version === 'string'
    && typeof info.idf === 'string' && typeof info.protocol === 'number' && typeof info.maxPayload === 'number'
    && typeof info.dapPacket === 'number' && typeof info.leaseActive === 'boolean' && typeof info.allowedPins === 'number'
    && typeof info.freeHeap === 'number' && typeof info.minHeap === 'number' && typeof info.otaAvailable === 'boolean'
    && typeof info.pendingBoot === 'boolean' && typeof info.bandStage === 'number' && Array.isArray(info.capabilities)
    && typeof wifiInfo.ip === 'string' && typeof wifiInfo.ap === 'boolean'
    && typeof wifiInfo.channel === 'number' && typeof wifiInfo.rssi === 'number' && typeof wifiInfo.phy === 'number';
}
