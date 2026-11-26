import { createServer } from 'node:net';
import type { AddressInfo, Socket } from 'node:net';
import { OP } from './client.ts';
import { unpadDap } from './dap.ts';

export type UsbKind = 'dap' | 'uart' | 'spi' | 'i2c';
export interface UsbUartConfiguration { count: number; hz: number; pins: number[] }
interface PendingRead { maximum: number; resolve: (value: Buffer) => void; reject: (reason?: unknown) => void; cleanup: () => void }
export interface UsbIpServer { port: number; readonly connected: boolean; close(): void }
interface UsbRequestFailure extends Error { errno: number }
const stall = (): never => { const error = new Error('Unsupported USB request.') as UsbRequestFailure; error.errno = -32; throw error; };
const stringDescriptor = (text: string): Buffer => { const value = Buffer.from(text, 'utf16le'); return Buffer.concat([Buffer.from([value.length + 2, 3]), value]); };

export class C5UsbModel {
  readonly kind: UsbKind;
  readonly identity: string;
  readonly command: (op: number, payload: Buffer) => Promise<Buffer>;
  uart: UsbUartConfiguration | null;
  configuration: number;
  response: Buffer | null;
  reader: PendingRead | null;
  active: boolean;
  queue: Promise<unknown>;
  queued: number;
  readonly strings: string[];
  readonly device: Buffer;
  readonly config: Buffer;

  constructor(kind: UsbKind, identity: string, command: (op: number, payload: Buffer) => Promise<Buffer>, uart: UsbUartConfiguration | null = null) {
    if (!['dap', 'uart', 'spi', 'i2c'].includes(kind)) throw new Error('Invalid USB model.');
    this.kind = kind; this.identity = identity; this.command = command; this.uart = uart;
    this.configuration = 0; this.response = null; this.reader = null; this.active = false; this.queue = Promise.resolve(); this.queued = 0;
    this.strings = ['TetherBench', kind === 'dap' ? 'TetherBench CMSIS-DAP v2' : 'TetherBench ' + kind.toUpperCase(), identity, kind === 'dap' ? 'CMSIS-DAP v2' : kind.toUpperCase()];
    // shortcut: laboratory VID/PID only, assign project IDs before distributing USB devices.
    this.device = Buffer.from([18, 1, 0, 2, kind === 'uart' ? 0xef : 0, kind === 'uart' ? 2 : 0, kind === 'uart' ? 1 : 0, 64, 9, 18, 1, 0, 0, 1, 1, 2, 3, 1]);
    this.config = kind === 'uart' ? Buffer.from([9,2,75,0,2,1,0,0x80,50, 8,11,0,2,2,2,1,4, 9,4,0,0,1,2,2,1,4, 5,0x24,0,0x10,1, 5,0x24,1,0,1, 4,0x24,2,2, 5,0x24,6,0,1, 7,5,0x82,3,16,0,16, 9,4,1,0,2,10,0,0,4, 7,5,1,2,0,2,0, 7,5,0x81,2,0,2,0])
      : Buffer.from([9,2,32,0,1,1,0,0x80,50, 9,4,0,0,2,0xff,0,0,4, 7,5,1,2,0,2,0, 7,5,0x81,2,0,2,0]);
  }
  serial<T>(work: () => Promise<T>): Promise<T> {
    if (this.queued >= 4) return Promise.reject(new Error('USB transaction queue is full.'));
    this.queued++; const result = this.queue.then(work); this.queue = result.catch(() => {});
    return result.finally(() => { this.queued--; });
  }
  async control(setup: Buffer, data: Buffer): Promise<Buffer> {
    const requestType = setup[0], request = setup[1], value = setup.readUInt16LE(2), index = setup.readUInt16LE(4), length = setup.readUInt16LE(6);
    let response: Buffer = Buffer.alloc(0);
    if (requestType === 0x80 && request === 6) {
      const type = value >> 8, id = value & 255;
      if (type === 1 && id === 0) response = this.device;
      else if (type === 2 && id === 0) response = this.config;
      else if (type === 3 && id === 0) response = Buffer.from([4,3,9,4]);
      else if (type === 3 && id >= 1 && id <= 4) response = stringDescriptor(this.strings[id - 1]);
      else if (type === 3 && id === 0xee && this.kind !== 'uart') response = Buffer.concat([Buffer.from([18,3]), Buffer.from('MSFT100', 'utf16le'), Buffer.from([0x20,0])]);
      else stall();
    } else if (requestType === 0xc0 && request === 0x20 && index === 4 && this.kind !== 'uart') {
      response = Buffer.alloc(40); response.writeUInt32LE(40); response.writeUInt16LE(0x0100, 4); response.writeUInt16LE(4, 6); response[8] = 1; response[17] = 1; response.write('WINUSB', 18);
    } else if (requestType === 0 && request === 9 && index === 0 && length === 0 && value <= 1) { this.configuration = value; }
    else if (requestType === 0 && request === 5 && value <= 127 && index === 0 && length === 0) { /* USB/IP keeps the remote address. */ }
    else if (requestType === 0x80 && request === 8 && value === 0 && index === 0) response = Buffer.from([this.configuration]);
    else if ([0x80, 0x81, 0x82].includes(requestType) && request === 0) response = Buffer.alloc(2);
    else if (requestType === 0x81 && request === 10 && index < (this.kind === 'uart' ? 2 : 1)) response = Buffer.from([0]);
    else if (requestType === 1 && request === 11 && value === 0 && index < (this.kind === 'uart' ? 2 : 1) && !length) {}
    else if (requestType === 2 && request === 1 && value === 0 && !length) {}
    else if (this.kind === 'uart' && index === 0 && requestType === 0xa1 && request === 0x21) { response = Buffer.alloc(7); response.writeUInt32LE(this.uart?.hz ?? 115200); response[6] = 8; }
    else if (this.kind === 'uart' && index === 0 && requestType === 0x21 && request === 0x20 && length === 7 && data.length === 7) {
      const uart = this.uart;
      if (!uart || !uart.count) return stall();
      if (data[4] || data[5] || data[6] !== 8 || !data.readUInt32LE() || data.readUInt32LE() > 5000000) return stall();
      const pins = Buffer.alloc(12); pins[0] = 2; pins[1] = uart.count; pins.writeUInt32LE(data.readUInt32LE(), 4);
      uart.pins.forEach((pin, i) => pins.writeInt8(pin, 8 + i));
      await this.serial(() => this.command(OP.PINS, pins)); uart.hz = data.readUInt32LE();
    } else if (this.kind === 'uart' && index === 0 && requestType === 0x21 && request === 0x22 && !length && value <= 3) {}
    else stall();
    return response.subarray(0, length);
  }
  finishRead(): void {
    if (!this.reader || !this.response) return; const { maximum, resolve, cleanup } = this.reader;
    if (maximum < this.response.length) { const { reject } = this.reader; cleanup(); this.reader = null; reject(new Error('USB receive buffer is too small.')); return; }
    const result = this.response; this.response = null; this.reader = null; cleanup(); resolve(result);
  }
  waitRead(maximum: number, signal: AbortSignal): Promise<Buffer> {
    if (this.reader) return Promise.reject(new Error('Only one bulk read may wait.'));
    return new Promise<Buffer>((resolve, reject) => {
      const cancel = () => { this.reader = null; reject(new Error('USB read cancelled.')); };
      const cleanup = () => signal.removeEventListener('abort', cancel);
      this.reader = { maximum, resolve, reject, cleanup }; signal.addEventListener('abort', cancel, { once: true });
      if (signal.aborted) cancel(); else this.finishRead();
    });
  }
  bus(protocol: number, data: Buffer, receive = 0): Promise<Buffer> {
    const header = Buffer.alloc(12); header[0] = protocol; header.writeUInt16LE(data.length, 4); header.writeUInt16LE(receive, 6); header.writeUInt16LE(receive ? 20 : 100, 8);
    return this.command(OP.BUS, Buffer.concat([header, data]));
  }
  async transfer(endpoint: number, direction: number, data: Buffer, maximum: number, signal: AbortSignal): Promise<Buffer | number> {
    if (!this.configuration || endpoint !== 1) {
      if (this.kind === 'uart' && this.configuration && endpoint === 2 && direction === 1) return this.waitRead(maximum, signal);
      stall();
    }
    if (this.kind === 'uart') {
      if (!this.uart?.count) stall();
      return this.serial(async () => {
        if (signal.aborted) throw new Error('USB request cancelled.');
        if (direction) return this.bus(2, Buffer.alloc(0), Math.min(maximum, 1024));
        for (let offset = 0; offset < data.length; offset += 1024) { if (signal.aborted) throw new Error('USB write cancelled.'); await this.bus(2, data.subarray(offset, offset + 1024)); }
        return data.length;
      });
    }
    if (direction) return this.waitRead(maximum, signal);
    if (this.active || this.response) throw new Error('Previous USB command is not consumed.');
    this.active = true;
    try {
      let response;
      if (this.kind === 'dap') response = await this.serial(() => this.command(OP.DAP, unpadDap(data)));
      else { if (data.length < 12 || data.length > 1036 || data[0] !== (this.kind === 'spi' ? 3 : 4)) stall(); response = await this.serial(() => this.command(OP.BUS, data)); }
      this.response = response; this.finishRead(); return data.length;
    } finally { this.active = false; }
  }
  close(): void { if (this.reader) { const reader = this.reader; this.reader = null; reader.cleanup(); reader.reject(new Error('USB device detached.')); } this.response = null; this.configuration = 0; }
}

export function startUsbIp(modelFactory: () => C5UsbModel, identity: string, kind: UsbKind): Promise<UsbIpServer> {
  let imported: Socket | null = null, stopped = false; const sockets = new Set<Socket>();
  const interfaces: number[][] = kind === 'uart' ? [[2,2,1,0], [10,0,0,0]] : [[0xff,0,0,0]];
  const device = Buffer.alloc(312); device.write('/tetherbench/' + identity); device.write('1-1', 256); device.writeUInt32BE(1, 288); device.writeUInt32BE(1, 292); device.writeUInt32BE(3, 296); device.writeUInt16BE(0x1209, 300); device.writeUInt16BE(1, 302); device.writeUInt16BE(0x0100, 304);
  if (kind === 'uart') { device[306] = 0xef; device[307] = 2; device[308] = 1; } device[309] = 1; device[310] = 1; device[311] = interfaces.length;
  const operation = (code: number, status = 0): Buffer => { const header = Buffer.alloc(8); header.writeUInt16BE(0x0111); header.writeUInt16BE(code, 2); header.writeUInt32BE(status, 4); return header; };
  const server = createServer((socket: Socket) => {
    if (sockets.size >= 4 || stopped) { socket.destroy(); return; } sockets.add(socket); socket.setNoDelay(true); socket.setTimeout(10000, () => socket.destroy());
    let buffer = Buffer.alloc(0), model: C5UsbModel | null = null; const pending = new Map<number, AbortController>();
    socket.on('error', () => {});
    socket.on('close', () => { sockets.delete(socket); if (imported === socket) imported = null; for (const job of pending.values()) job.abort(); pending.clear(); model?.close(); });
    const reply = (request: Buffer, command: number, status: number, data: Buffer = Buffer.alloc(0), actual = data.length): void => {
      if (socket.destroyed) return; const header = Buffer.alloc(48); request.copy(header, 0, 0, 20); header.writeUInt32BE(command); header.writeInt32BE(status, 20); if (command === 3) header.writeUInt32BE(actual, 24);
      socket.write(request.readUInt32BE(12) && command === 3 && !status ? Buffer.concat([header, data]) : header);
    };
    function submit(header: Buffer, data: Buffer): void {
      const sequence = header.readUInt32BE(4), direction = header.readUInt32BE(12), endpoint = header.readUInt32BE(16), maximum = header.readInt32BE(24);
      if (pending.has(sequence) || pending.size >= 16) { socket.destroy(); return; }
      const controller = new AbortController(); pending.set(sequence, controller);
      const activeModel = model;
      if (!activeModel) { pending.delete(sequence); socket.destroy(); return; }
      Promise.resolve().then(async (): Promise<Buffer | number> => {
        if (controller.signal.aborted) throw new Error('USB request cancelled.');
        if (endpoint === 0) { if ((header[40] >> 7) !== direction || header.readUInt16LE(46) !== maximum) stall(); const result = await activeModel.control(header.subarray(40,48), data); return direction ? result : data.length; }
        return activeModel.transfer(endpoint, direction, data, maximum, controller.signal);
      }).then((result) => {
        if (!pending.has(sequence)) return;
        pending.delete(sequence);
        if (direction) {
          if (!Buffer.isBuffer(result) || result.length > maximum) throw new Error('Invalid USB result.');
          reply(header, 3, 0, result);
        } else reply(header, 3, 0, Buffer.alloc(0), typeof result === 'number' ? result : data.length);
      }).catch((error: unknown) => {
        if (pending.delete(sequence)) {
          const errno = typeof error === 'object' && error !== null && 'errno' in error && typeof error.errno === 'number' ? error.errno : -5;
          reply(header, 3, errno);
        }
      });
    }
    socket.on('data', (chunk: Buffer) => {
      if (buffer.length + chunk.length > 131072 || socket.writableLength > 131072) { socket.destroy(); return; } buffer = Buffer.concat([buffer, chunk]);
      try { while (buffer.length) {
        if (!model) {
          if (buffer.length < 8) break; const version = buffer.readUInt16BE(), code = buffer.readUInt16BE(2);
          if (version !== 0x0111 || buffer.readUInt32BE(4) || ![0x8005, 0x8003].includes(code)) throw new Error('Invalid USB/IP operation.');
          if (code === 0x8005) { const count = Buffer.alloc(4); count.writeUInt32BE(imported ? 0 : 1); socket.end(imported ? Buffer.concat([operation(5), count]) : Buffer.concat([operation(5), count, device, ...interfaces.map((value) => Buffer.from(value))])); buffer = Buffer.alloc(0); break; }
          if (buffer.length < 40) break; const busid = buffer.subarray(8,40).toString('ascii').split(String.fromCharCode(0))[0]; buffer = buffer.subarray(40);
          if (busid !== '1-1' || imported) { socket.end(operation(3, 1)); break; }
          imported = socket; model = modelFactory(); socket.setTimeout(0); socket.write(Buffer.concat([operation(3), device]));
        } else {
          if (buffer.length < 48) break; const command = buffer.readUInt32BE(), header = Buffer.from(buffer.subarray(0,48));
          if (![1,2].includes(command) || header.readUInt32BE(8) !== 0x10001 || header.readUInt32BE(12) > 1 || header.readUInt32BE(16) > 15) throw new Error('Invalid USB/IP header.');
          if (command === 2) { buffer = buffer.subarray(48); const target = header.readUInt32BE(20), job = pending.get(target); if (job) { pending.delete(target); job.abort(); } reply(header, 4, job ? -104 : 0); continue; }
          const length = header.readInt32BE(24); if (length < 0 || length > 16384 || header.readInt32BE(32) !== 0) throw new Error('Unsupported USB/IP transfer.');
          const wireSize = 48 + (header.readUInt32BE(12) ? 0 : length); if (buffer.length < wireSize) break;
          const data = Buffer.from(buffer.subarray(48, wireSize)); buffer = buffer.subarray(wireSize); submit(header, data);
        }
      } socket.setTimeout(buffer.length || !model ? 10000 : 0); } catch { socket.destroy(); }
    });
  });
  return new Promise<UsbIpServer>((resolve, reject) => {
    server.once('error', reject); server.listen(0, '127.0.0.1', () => {
      server.removeListener('error', reject); server.on('error', () => { for (const socket of sockets) socket.destroy(); });
      const address = server.address();
      if (!address || typeof address === 'string') { reject(new Error('USB/IP listener has no TCP address.')); return; }
      resolve({ port: (address as AddressInfo).port, get connected() { return Boolean(imported); }, close() { if (stopped) return; stopped = true; for (const socket of sockets) socket.destroy(); server.close(); } });
    });
  });
}
