import { createServer } from 'node:net';
import type { AddressInfo, Socket } from 'node:net';
import { ControlError } from './c5.ts';
import type { C5AdapterEntry, C5AdapterType, C5Registry } from './c5.ts';
import { OP } from '../../firmware/probe-c5/tools/client.ts';
import { C5UsbModel, startUsbIp } from '../../firmware/probe-c5/tools/usbip.ts';

export interface DapTcpServer {
  port: number;
  readonly connected: boolean;
  close(): void;
}

interface UsbUartConfiguration { count: number; hz: number; pins: number[] }
const UsbModel = C5UsbModel as unknown as new (kind: string, identity: string, command: (op: number, payload: Buffer) => Promise<Buffer>, uart: UsbUartConfiguration | null) => unknown;
const listenUsbIp = startUsbIp as unknown as (modelFactory: () => unknown, identity: string, kind: string) => Promise<DapTcpServer>;

export function startDapTcp(command: (payload: Buffer) => Promise<Buffer>): Promise<DapTcpServer> {
  let client: Socket | null = null, stopped = false;
  const sockets = new Set<Socket>();
  const server = createServer((socket: Socket) => {
    if (client || stopped) { socket.destroy(); return; }
    client = socket; sockets.add(socket); socket.setNoDelay(true);
    let buffered = Buffer.alloc(0), processing = false;
    socket.on('error', () => {});
    socket.on('close', () => { sockets.delete(socket); if (client === socket) client = null; });
    socket.on('timeout', () => socket.destroy());
    async function pump() {
      if (processing || socket.destroyed) return; processing = true; socket.pause();
      try {
        while (buffered.length >= 8) {
          const count = buffered.readUInt16LE(4);
          if (buffered.readUInt32LE() !== 0x00504144 || buffered[6] !== 1 || buffered[7] !== 0 || count < 1 || count > 1024) throw new Error('Invalid OpenOCD TCP frame.');
          if (buffered.length < 8 + count) break;
          const request = Buffer.from(buffered.subarray(8, 8 + count)); buffered = buffered.subarray(8 + count);
          const response = await command(request);
          if (!Buffer.isBuffer(response) || response.length > 1024 || !response.length) throw new Error('Invalid CMSIS-DAP response.');
          if (socket.destroyed || stopped) return;
          const header = Buffer.alloc(8); header.writeUInt32LE(0x00504144); header.writeUInt16LE(response.length, 4); header[6] = 2;
          socket.write(Buffer.concat([header, response]));
        }
        socket.setTimeout(buffered.length ? 10000 : 0);
      } catch { socket.destroy(); }
      finally { processing = false; if (!socket.destroyed) socket.resume(); }
    }
    socket.on('data', (chunk) => {
      if (buffered.length + chunk.length > 16 * 1032) { socket.destroy(); return; }
      buffered = Buffer.concat([buffered, chunk]); pump();
    });
  });
  return new Promise<DapTcpServer>((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      server.removeListener('error', reject); server.on('error', () => { for (const socket of sockets) socket.destroy(); });
      const address = server.address();
      if (!address || typeof address === 'string') { reject(new Error('Dap TCP listener has no TCP address.')); return; }
      resolve({ port: (address as AddressInfo).port, get connected() { return Boolean(client); },
        close() { if (stopped) return; stopped = true; for (const socket of sockets) socket.destroy(); server.close(); } });
    });
  });
}

export class C5Adapters {
  readonly registry: C5Registry;
  readonly entries: Map<string, C5AdapterEntry>;
  readonly timer: ReturnType<typeof setInterval>;
  constructor(registry: C5Registry) {
    this.registry = registry; this.entries = new Map<string, C5AdapterEntry>();
    this.timer = setInterval(() => this.sweep(), 1000); this.timer.unref();
  }
  sweep(): void {
    for (const [id, entry] of this.entries) {
      const record = this.registry.record(id);
      if (!record.connected || record.session?.id !== entry.sessionId || (record.session?.deadline ?? 0) <= this.registry.now()) this.stop(id);
    }
  }
  list(id: string) { const entry = this.entries.get(id); return entry?.server ? [{ type: entry.type, host: '127.0.0.1', port: entry.server.port, connected: entry.server.connected }] : []; }
  async start(id: string, sessionId: string, type: C5AdapterType): Promise<{ type: C5AdapterType; host: string; port: number; connected: boolean; configuration: string }> {
    if (!['openocd', 'usb-dap', 'usb-uart', 'usb-spi', 'usb-i2c'].includes(type)) throw new ControlError(400, 'adapter_type', 'Unsupported C5 adapter.');
    return this.registry.run(id, async (record) => {
      this.registry.owner(record, sessionId);
      if (this.entries.has(id)) throw new ControlError(409, 'adapter_active', 'Stop the current adapter first.');
      const entry: C5AdapterEntry = { type, sessionId, server: null };
      const command = (op: number, payload: Buffer) => this.registry.command(id, sessionId, op, payload, { adapter: entry, wait: true });
      let server: NonNullable<C5AdapterEntry['server']>;
      const info = record.info;
      if (!info) throw new ControlError(503, 'device_offline', 'C5 device information is unavailable.');
      if (type === 'openocd') server = await startDapTcp((payload) => command(OP.DAP, payload));
      else {
        let uart: UsbUartConfiguration | null = null;
        if (type === 'usb-uart') {
          const data = await this.registry.client(record).rpc(OP.CONFIG, Buffer.alloc(0), 0n);
          if (data.length !== 48 || !data[13]) throw new ControlError(409, 'uart_unconfigured', 'Configure UART pins before starting CDC ACM.');
          uart = { count: data[13], hz: data.readUInt32LE(16), pins: [0,1,2,3].map((index) => data.readInt8(20 + index)) };
        }
        server = await listenUsbIp(() => new UsbModel(type.slice(4), info.id, command, uart), info.id, type.slice(4));
      }
      entry.server = server;
      record.adapter = entry; this.entries.set(id, entry);
      const configuration = type === 'openocd' ? ['adapter driver cmsis-dap', 'cmsis-dap backend tcp', 'cmsis-dap tcp host 127.0.0.1', 'cmsis-dap tcp port ' + server.port, 'cmsis-dap tcp min_timeout 5500', 'transport select swd', 'adapter speed 1000', '', 'pyOCD 0.46.1:', 'python firmware/probe-c5/tools/pyocd_probe.py --endpoint ' + id + '=' + server.port + ' -- list'].join(String.fromCharCode(10))
        : 'USB/IP 127.0.0.1:' + server.port + ', busid 1-1' + String.fromCharCode(10) + '需要安装支持指定端口的 USB/IP 客户端驱动。仅本机访问；挂载与系统枚举仍需验证。';
      return { ...this.list(id)[0], configuration };
    });
  }
  stop(id: string, sessionId?: string): { stopped: true } {
    const record = this.registry.record(id); if (sessionId !== undefined) this.registry.owner(record, sessionId);
    const entry = this.entries.get(id); if (entry) { entry.server?.close(); this.entries.delete(id); record.adapter = null; }
    return { stopped: true };
  }
  close(): void { clearInterval(this.timer); for (const id of this.entries.keys()) this.stop(id); }
}
