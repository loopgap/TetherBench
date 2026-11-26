import assert from 'node:assert/strict';
import { once } from 'node:events';
import { connect } from 'node:net';
import type { Socket } from 'node:net';
import { test } from 'node:test';
import { C5UsbModel, startUsbIp } from '../firmware/probe-c5/tools/usbip.ts';
import { unpadDap } from '../firmware/probe-c5/tools/dap.ts';
import { OP } from '../firmware/probe-c5/tools/client.ts';

const setup = (type: number, request: number, value: number, index: number, length: number): Buffer => { const data = Buffer.alloc(8); data[0] = type; data[1] = request; data.writeUInt16LE(value, 2); data.writeUInt16LE(index, 4); data.writeUInt16LE(length, 6); return data; };
const operation = (code: number): Buffer => { const data = Buffer.alloc(8); data.writeUInt16BE(0x0111); data.writeUInt16BE(code, 2); return data; };
function submit(sequence: number, direction: number, endpoint: number, length: number, control: Buffer = Buffer.alloc(8), data: Buffer = Buffer.alloc(0)): Buffer {
  const header = Buffer.alloc(48); header.writeUInt32BE(1); header.writeUInt32BE(sequence, 4); header.writeUInt32BE(0x10001, 8); header.writeUInt32BE(direction, 12); header.writeUInt32BE(endpoint, 16); header.writeUInt32BE(length, 24); control.copy(header, 40); return Buffer.concat([header, data]);
}
async function read(socket: Socket, count: number): Promise<Buffer> {
  const chunks: Buffer[] = []; let size = 0;
  while (size < count) { const chunk = socket.read(count - size); if (chunk) { chunks.push(chunk); size += chunk.length; } else { if (socket.readableEnded || socket.destroyed) throw new Error('USB/IP disconnected'); await once(socket, 'readable'); } }
  return Buffer.concat(chunks);
}

test('USB model returns CMSIS-DAP descriptors, strips only zero padding and cancels pending reads', async () => {
  const calls: Array<[number, Buffer]> = []; const model = new C5UsbModel('dap', 'c5-001122334455', async (op, data) => { calls.push([op, data]); return Buffer.from([0,1,1]); });
  const device = await model.control(setup(0x80, 6, 0x0100, 0, 18), Buffer.alloc(0)); assert.equal(device[0], 18);
  const config = await model.control(setup(0x80, 6, 0x0200, 0, 255), Buffer.alloc(0)); assert.equal(config.length, config.readUInt16LE(2)); assert.equal(config[14], 0xff);
  const name = await model.control(setup(0x80, 6, 0x0304, 0x409, 255), Buffer.alloc(0)); assert.equal(name.subarray(2).toString('utf16le'), 'CMSIS-DAP v2');
  assert.equal((await model.control(setup(0xc0, 0x20, 0, 4, 40), Buffer.alloc(0))).toString('ascii', 18, 24), 'WINUSB');
  await model.control(setup(0, 9, 1, 0, 0), Buffer.alloc(0));
  const signal = new AbortController(), waiting = model.transfer(1, 1, Buffer.alloc(0), 1024, signal.signal);
  const packet = Buffer.alloc(1024); packet[1] = 0xf0; await model.transfer(1, 0, packet, packet.length, signal.signal);
  assert.deepEqual(await waiting, Buffer.from([0,1,1])); assert.deepEqual(calls[0], [OP.DAP, Buffer.from([0,0xf0])]);
  const cancelled = model.transfer(1, 1, Buffer.alloc(0), 1024, signal.signal); signal.abort(); await assert.rejects(cancelled, /cancelled/); model.close();
  assert.deepEqual(unpadDap(Buffer.from([0x09,1,0,0,0])), Buffer.from([0x09,1,0]));
  assert.throws(() => unpadDap(Buffer.from([0,0xf0,1]))); assert.throws(() => unpadDap(Buffer.from([5,0,1,0])));
});

test('CDC line coding uses configured UART pins, rejects unsupported framing and bounds bulk transfers', async () => {
  const calls: Array<[number, Buffer]> = []; const uart = { count: 2, hz: 115200, pins: [2,3,-1,-1] };
  const model = new C5UsbModel('uart', 'c5-001122334455', async (op, data) => { calls.push([op, Buffer.from(data)]); return Buffer.alloc(op === OP.BUS ? data.readUInt16LE(6) : 0, 0x55); }, uart);
  const config = await model.control(setup(0x80,6,0x0200,0,255), Buffer.alloc(0)); assert.equal(config.length, 75); assert.equal(config.readUInt16LE(2), 75);
  await model.control(setup(0,9,1,0,0), Buffer.alloc(0));
  const coding = Buffer.alloc(7); coding.writeUInt32LE(230400); coding[6] = 8;
  await model.control(setup(0x21,0x20,0,0,7), coding); assert.equal(calls[0][0], OP.PINS); assert.equal(calls[0][1].readUInt32LE(4), 230400);
  coding[5] = 1; await assert.rejects(model.control(setup(0x21,0x20,0,0,7), coding), { errno: -32 });
  const signal = new AbortController().signal;
  assert.equal(await model.transfer(1,0,Buffer.alloc(2500),2500,signal), 2500);
  assert.deepEqual(calls.slice(1).map((call) => call[1].length - 12), [1024,1024,452]);
  const readBack = await model.transfer(1,1,Buffer.alloc(0),2048,signal);
  assert.ok(Buffer.isBuffer(readBack)); if (Buffer.isBuffer(readBack)) assert.equal(readBack.length,1024); model.close();
});

test('USB/IP lists, imports, enumerates, completes bulk URBs and unlinks without a second completion', async () => {
  const server = await startUsbIp(() => new C5UsbModel('dap', 'c5-001122334455', async () => Buffer.from([0,1,1])), 'c5-001122334455', 'dap');
  let socket: Socket | undefined;
  try {
    const list = connect(server.port, '127.0.0.1'); await once(list,'connect'); list.write(operation(0x8005)); const listing = await read(list,328); assert.equal(listing.readUInt32BE(8),1); assert.equal(listing[324],0xff); list.destroy();
    socket = connect(server.port,'127.0.0.1'); await once(socket,'connect'); const bus = Buffer.alloc(32); bus.write('1-1'); socket.write(Buffer.concat([operation(0x8003),bus])); const imported = await read(socket,320); assert.equal(imported.readUInt32BE(4),0);
    socket.write(submit(1,1,0,18,setup(0x80,6,0x0100,0,18))); const descriptor = await read(socket,66); assert.equal(descriptor.readInt32BE(20),0); assert.equal(descriptor[48],18);
    socket.write(submit(2,0,0,0,setup(0,9,1,0,0))); assert.equal((await read(socket,48)).readUInt32BE(4),2);
    socket.write(submit(3,1,1,1024)); socket.write(submit(4,0,1,2,Buffer.alloc(8),Buffer.from([0,0xf0])));
    const completions = new Map();
    for (let index = 0; index < 2; index++) {
      const header = await read(socket,48); assert.equal(header.readInt32BE(20),0);
      completions.set(header.readUInt32BE(4), header.readUInt32BE(12) ? await read(socket, header.readUInt32BE(24)) : Buffer.alloc(0));
    }
    assert.deepEqual(completions.get(3), Buffer.from([0,1,1])); assert.ok(completions.has(4));
    socket.write(submit(5,1,1,1024)); const unlink = Buffer.alloc(48); unlink.writeUInt32BE(2); unlink.writeUInt32BE(6,4); unlink.writeUInt32BE(0x10001,8); unlink.writeUInt32BE(5,20); socket.write(unlink);
    const unlinked = await read(socket,48); assert.equal(unlinked.readUInt32BE(),4); assert.equal(unlinked.readInt32BE(20),-104);
    socket.write(submit(7,1,0,1,setup(0x80,8,0,0,1))); const next = await read(socket,49); assert.equal(next.readUInt32BE(4),7); assert.equal(next[48],1);
    socket.write(submit(8,1,1,20000)); await once(socket,'close');
  } finally { socket?.destroy(); server.close(); }
});
