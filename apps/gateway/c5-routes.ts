import type { IncomingMessage, ServerResponse } from 'node:http';
import type { Readable } from 'node:stream';
import { ControlError, decodeC5Ota } from './c5.ts';
import type { C5OtaState, C5Registry } from './c5.ts';
import type { C5AdapterType } from './c5.ts';
import type { C5Adapters } from './c5-adapters.ts';
import { decodeTargetInfo, decodeTargetOta, OP } from '../../firmware/probe-c5/tools/client.ts';
import type { C5TargetInfo, C5TargetOtaState } from '../../firmware/probe-c5/tools/client.ts';

type JsonReader = (request: IncomingMessage) => Promise<unknown>
type JsonWriter = (response: ServerResponse, status: number, value: unknown) => void
interface C5RouteContext {
  request: IncomingMessage; response: ServerResponse; pathname: string; registry: C5Registry;
  adapters: C5Adapters; readJson: JsonReader; json: JsonWriter;
}
type JsonObject = Record<string, unknown>
function fail(status: number, code: string, message: string): never { throw new ControlError(status, code, message); }
function isRecord(value: unknown): value is JsonObject { return Boolean(value) && typeof value === 'object' && !Array.isArray(value); }
function hex(value: unknown, maximum: number): Buffer {
  if (typeof value !== 'string' || value.length > maximum * 2 || !/^(?:[a-fA-F0-9]{2})*$/.test(value)) {
    fail(400, 'invalid_hex', 'Data must contain complete hexadecimal byte pairs within the transaction limit.');
  }
  return Buffer.from(value, 'hex');
}
function integer(value: unknown, min: number, max: number, name: string): number {
  if (typeof value !== 'number' || !Number.isInteger(value) || value < min || value > max) fail(400, 'invalid_parameter', `${name} is outside its allowed range.`);
  return value;
}
function empty(request: IncomingMessage): void {
  if (request.headers['transfer-encoding'] || Number(request.headers['content-length'] ?? 0) !== 0) {
    fail(400, 'unexpected_body', 'This operation has no request body.');
  }
}
function targetProtocol(value: unknown): 2 | 3 {
  if (value === 2 || value === 3 || value === '2' || value === '3') return Number(value) as 2 | 3;
  if (value === 'uart') return 2;
  if (value === 'spi') return 3;
  fail(400, 'invalid_target_protocol', 'Target OTA protocol must be UART or SPI.');
}
function targetJob(value: unknown): bigint {
  if (typeof value !== 'string' || !/^[0-9]{1,20}$/.test(value) || BigInt(value) === 0n || BigInt(value) > 0xffffffffffffffffn) {
    fail(400, 'invalid_job', 'jobId must be a nonzero unsigned 64-bit decimal string.');
  }
  return BigInt(value);
}
export async function uploadC5(registry: C5Registry, id: string, sessionId: string, input: AsyncIterable<Buffer> & Pick<Readable, 'destroy'>, declaredSize: number): Promise<C5OtaState> {
  return registry.run(id, async (record) => {
    const session = registry.owner(record, sessionId);
    if (record.adapter) fail(409, 'adapter_active', 'Stop the active adapter before updating firmware.');
    const client = registry.client(record);
    let pending = Buffer.alloc(0), manifest: Buffer | null = null, total = 0, inputBytes = 0, imageOffset = 0, received = 0, renewed = Date.now();
    const timer = setTimeout(() => input.destroy(new Error('OTA upload exceeded three minutes.')), 180000); timer.unref();
    try {
      for await (const chunk of uploadChunks(input)) {
        session.deadline = registry.now() + 60000;
        inputBytes += chunk.length;
        if (inputBytes > declaredSize) fail(413, 'ota_size', 'OTA upload exceeds declared length.');
        pending = Buffer.concat([pending, chunk]);
        if (!manifest && pending.length >= 176) {
          manifest = Buffer.from(pending.subarray(0, 176)); pending = pending.subarray(176);
          total = manifest.readUInt32LE(12);
          if (total < 288 || total > 0x1e0000 || declaredSize !== 176 + total) fail(400, 'ota_size', 'Manifest and upload lengths do not match.');
          record.ota = decodeC5Ota(await client.rpc(OP.OTA_BEGIN, manifest), total);
          if (record.ota.total !== total || record.ota.received > total) throw new Error('Invalid OTA resume offset.');
          received = record.ota.received;
        }
        while (manifest && pending.length) {
          if (imageOffset < received) {
            const skip = Math.min(received - imageOffset, pending.length);
            imageOffset += skip; pending = pending.subarray(skip); continue;
          }
          if (pending.length < 4084 && inputBytes < declaredSize) break;
          const count = Math.min(4084, pending.length);
          const block = Buffer.alloc(12 + count); manifest.copy(block, 0, 0, 8); block.writeUInt32LE(imageOffset, 8); pending.copy(block, 12, 0, count);
          record.ota = decodeC5Ota(await client.rpc(OP.OTA_WRITE, block), total);
          imageOffset += count; received = imageOffset; pending = pending.subarray(count);
          if (Date.now() - renewed >= 10000) { await registry.renewOwned(record, sessionId); renewed = Date.now(); }
        }
      }
      if (!manifest || inputBytes !== declaredSize || imageOffset !== total) fail(400, 'ota_truncated', 'OTA upload was truncated.');
      const completedManifest = manifest;
      await client.rpc(OP.OTA_FINISH, completedManifest.subarray(0, 8));
      record.ota = decodeC5Ota(await client.rpc(OP.OTA_STATUS), total); return record.ota;
    } finally { clearTimeout(timer); }
  });
}

export async function uploadTarget(registry: C5Registry, id: string, sessionId: string, protocol: 2 | 3,
  input: AsyncIterable<Buffer> & Pick<Readable, 'destroy'>, declaredSize: number): Promise<C5TargetOtaState> {
  return registry.run(id, async (record) => {
    registry.owner(record, sessionId);
    if (record.adapter) fail(409, 'adapter_active', 'Stop the active adapter before updating the target.');
    const client = registry.client(record);
    const info: C5TargetInfo = decodeTargetInfo(await client.rpc(OP.TARGET_INFO, Buffer.from([protocol])));
    if ((info.capabilities & 0xff) !== 0xff) fail(422, 'target_unsupported', 'The target does not provide all required OTA capabilities.');
    if (info.maxChunk > 983) fail(502, 'target_protocol', 'The target reports an unsupported write chunk size.');
    let pending = Buffer.alloc(0), manifest: Buffer | null = null, initialState: C5TargetOtaState | null = null, total = 0, inputBytes = 0, imageOffset = 0, received = 0, checkpoint = 0, resumable = false, renewed = Date.now(), targetJobId = 0n;
    const timer = setTimeout(() => input.destroy(new Error('Target OTA upload exceeded three minutes.')), 180000); timer.unref();
    try {
      for await (const chunk of uploadChunks(input)) {
        if (chunk.length > 0) { inputBytes += chunk.length; if (inputBytes > declaredSize) fail(413, 'target_ota_size', 'Target OTA upload exceeds declared length.'); }
        pending = Buffer.concat([pending, chunk]);
        if (!manifest && pending.length >= 184) {
          manifest = Buffer.from(pending.subarray(0, 184)); pending = pending.subarray(184);
          targetJobId = manifest.readBigUInt64LE(0); total = manifest.readUInt32LE(12);
          if (!targetJobId || manifest[8] !== 1 || manifest[9] !== 0 || manifest.readUInt16LE(10) !== 0 || total < 1 || total > info.maxImageLength || declaredSize !== 184 + total) {
            fail(400, 'target_ota_manifest', 'Target manifest or upload length is invalid.');
          }
          const boardField = manifest.subarray(80, 112), boardEnd = boardField.indexOf(0);
          if (boardEnd < 0 || boardField.subarray(boardEnd + 1).some((value) => value !== 0)) {
            fail(400, 'target_ota_manifest', 'Target manifest board ID is not zero padded.');
          }
          if (boardField.subarray(0, boardEnd).toString('utf8') !== info.boardId) fail(409, 'target_board_mismatch', 'Target manifest board ID does not match the connected target.');
          const begin = Buffer.alloc(185); begin[0] = protocol; manifest.copy(begin, 1);
          const state = decodeTargetOta(await client.rpc(OP.TARGET_OTA_BEGIN, begin));
          if (state.jobId !== targetJobId.toString() || state.total !== total || ![1, 2, 4, 5, 6].includes(state.phase)) fail(502, 'target_ota_state', 'Target returned an invalid resume state.');
          initialState = state;
          resumable = state.phase === 1;
          received = state.received; checkpoint = state.checkpoint;
        }
        while (manifest && pending.length) {
          if (!resumable) { pending = Buffer.alloc(0); break; }
          if (imageOffset < checkpoint) {
            const skip = Math.min(checkpoint - imageOffset, pending.length); imageOffset += skip; pending = pending.subarray(skip); continue;
          }
          if (pending.length < info.maxChunk && inputBytes < declaredSize) break;
          const replaying = imageOffset < received;
          const count = Math.min(info.maxChunk, pending.length, total - imageOffset, replaying ? received - imageOffset : total - imageOffset);
          if (!count) break;
          const block = Buffer.alloc(12 + count); block.writeBigUInt64LE(targetJobId, 0); block.writeUInt32LE(imageOffset, 8); pending.copy(block, 12, 0, count);
          const state = decodeTargetOta(await client.rpc(OP.TARGET_OTA_WRITE, block));
          const expectedReceived = replaying ? received : imageOffset + count;
          if (state.jobId !== targetJobId.toString() || state.phase !== 1 || state.total !== total || state.received !== expectedReceived || state.checkpoint < checkpoint) fail(502, 'target_ota_state', 'Target returned an invalid write checkpoint.');
          imageOffset += count; received = state.received; checkpoint = state.checkpoint; pending = pending.subarray(count);
          if (Date.now() - renewed >= 10000) { await registry.renewOwned(record, sessionId); renewed = Date.now(); }
        }
      }
      if (!manifest || inputBytes !== declaredSize || (resumable && imageOffset !== total) || pending.length) fail(400, 'target_ota_truncated', 'Target OTA upload was truncated.');
      if (!initialState) fail(502, 'target_ota_state', 'Target did not provide an update state.');
      if (!resumable) return initialState;
      const job = Buffer.alloc(8); job.writeBigUInt64LE(targetJobId);
      decodeTargetOta(await client.rpc(OP.TARGET_OTA_FINISH, job));
      return decodeTargetOta(await client.rpc(OP.TARGET_OTA_STATUS));
    } finally { clearTimeout(timer); }
  });
}

async function* uploadChunks(input: AsyncIterable<Buffer>): AsyncGenerator<Buffer> {
  try { for await (const chunk of input) yield chunk; }
  catch { throw new ControlError(408, 'ota_input_interrupted', 'OTA request ended before the upload completed.'); }
}
export async function handleC5({ request, response, pathname, registry, adapters, readJson, json }: C5RouteContext): Promise<boolean> {
  if (!pathname.startsWith('/api/v1/c5')) return false;
  if (request.method === 'GET' && pathname === '/api/v1/c5') {
    json(response, 200, { devices: registry.list(), discoveryError: registry.discoveryError ?? null,
      limits: { maxDevices: 16, maxPayload: 4096, maxBusBytes: 1024 } }); return true;
  }
  if (request.method === 'POST' && pathname === '/api/v1/c5/discover') {
    empty(request); await registry.refresh();
    json(response, 200, { devices: registry.list(), discoveryError: registry.discoveryError ?? null }); return true;
  }
  const match = pathname.match(new RegExp('^/api/v1/c5/([^/]+)(?:/(.*))?$'));
  if (!match) fail(404, 'not_found', 'C5 endpoint was not found.');
  const encodedId = match[1];
  if (encodedId === undefined) fail(404, 'not_found', 'C5 endpoint was not found.');
  const id = decodeURIComponent(encodedId), action = match[2] ?? '';
  registry.record(id);
  if (request.method === 'GET' && !action) { json(response, 200, await registry.status(id)); return true; }
  if (request.method === 'GET' && action === 'adapters') { json(response, 200, { adapters: adapters.list(id) }); return true; }
  if (request.method === 'POST' && action === 'session') { empty(request); json(response, 201, await registry.acquire(id)); return true; }
  const session = action.match(new RegExp('^session/([^/]+)(/renew)?$'));
  if (session) {
    empty(request); const sessionId = decodeURIComponent(session[1]);
    if (request.method === 'GET' && !session[2]) { json(response, 200, registry.session(id, sessionId)); return true; }
    if (request.method === 'DELETE' && !session[2]) { adapters.stop(id, sessionId); json(response, 200, await registry.release(id, sessionId)); return true; }
    if (request.method === 'POST' && session[2]) { json(response, 200, await registry.renew(id, sessionId)); return true; }
  }
  if (request.method === 'POST' && action === 'ota') {
    const length = Number(request.headers['content-length']);
    const sessionId = request.headers['x-tetherbench-session'];
    if (typeof sessionId !== 'string' || !sessionId) fail(400, 'invalid_session', 'A session ID is required for OTA.');
    if (request.headers['content-type'] !== 'application/octet-stream') fail(415, 'unsupported_media_type', 'Upload signed manifest followed by image bytes.');
    if (!Number.isSafeInteger(length) || length < 464 || length > 176 + 0x1e0000) fail(413, 'ota_size', 'OTA requires an explicit bounded Content-Length.');
    request.setTimeout(15000, () => request.destroy(new Error('OTA input stalled.')));
    json(response, 200, await uploadC5(registry, id, sessionId, request, length)); return true;
  }
  if (request.method === 'POST' && action === 'target-ota') {
    const length = Number(request.headers['content-length']);
    const sessionId = request.headers['x-tetherbench-session'];
    if (typeof sessionId !== 'string' || !sessionId) fail(400, 'invalid_session', 'A session ID is required for target OTA.');
    if (request.headers['content-type'] !== 'application/octet-stream') fail(415, 'unsupported_media_type', 'Upload the 184-byte target manifest followed by image bytes.');
    if (!Number.isSafeInteger(length) || length < 185 || length > 184 + 0x1e0000) fail(413, 'target_ota_size', 'Target OTA requires an explicit bounded Content-Length.');
    const protocol = targetProtocol(request.headers['x-tetherbench-protocol']);
    request.setTimeout(15000, () => request.destroy(new Error('Target OTA input stalled.')));
    json(response, 200, await uploadTarget(registry, id, sessionId, protocol, request, length)); return true;
  }
  if (request.method !== 'POST' && request.method !== 'PUT') fail(404, 'not_found', 'C5 endpoint was not found.');
  const value = await readJson(request);
  if (!isRecord(value) || typeof value.sessionId !== 'string') fail(400, 'invalid_session', 'A sessionId is required.');
  const body = value;
  const sessionId = body.sessionId as string;
  if (request.method === 'POST' && ['target-ota/info', 'target-ota/status', 'target-ota/activate', 'target-ota/abort'].includes(action)) {
    const protocol = body.protocol === undefined ? undefined : targetProtocol(body.protocol);
    if (action === 'target-ota/info') {
      if (protocol === undefined) fail(400, 'invalid_target_protocol', 'Target information requires UART or SPI.');
      json(response, 200, decodeTargetInfo(await registry.command(id, sessionId, OP.TARGET_INFO, Buffer.from([protocol])))); return true;
    }
    if (action === 'target-ota/status') {
      const payload = protocol === undefined ? Buffer.alloc(0) : Buffer.from([protocol]);
      json(response, 200, decodeTargetOta(await registry.command(id, sessionId, OP.TARGET_OTA_STATUS, payload))); return true;
    }
    const job = Buffer.alloc(8); job.writeBigUInt64LE(targetJob(body.jobId));
    const op = action.endsWith('activate') ? OP.TARGET_OTA_ACTIVATE : OP.TARGET_OTA_ABORT;
    json(response, 200, decodeTargetOta(await registry.command(id, sessionId, op, job))); return true;
  }
  if (request.method === 'POST' && action === 'wifi/negotiate') {
    const peerIdValue = body.peerId, requestedPeerSessionId = body.peerSessionId;
    if (typeof peerIdValue !== 'string' || (requestedPeerSessionId !== undefined && typeof requestedPeerSessionId !== 'string')) {
      fail(400, 'invalid_peer', 'peerId and an optional peerSessionId are required.');
    }
    const peerId = peerIdValue as string;
    if (peerId === id) fail(400, 'invalid_peer', 'Choose a different C5 device.');
    registry.record(peerId);
    let peerSessionId: string = requestedPeerSessionId ?? '', temporary = false;
    if (!peerSessionId) { peerSessionId = (await registry.acquire(peerId)).id; temporary = true; }
    try { json(response, 200, await registry.negotiateBand(id, sessionId, peerId, peerSessionId)); }
    finally { if (temporary) await registry.release(peerId, peerSessionId).catch(() => {}); }
    return true;
  }
  if (request.method === 'POST' && action === 'adapters') {
    const types: readonly string[] = ['openocd', 'usb-dap', 'usb-uart', 'usb-spi', 'usb-i2c'];
    const adapterType = body.type;
    if (typeof adapterType !== 'string' || !types.includes(adapterType)) fail(400, 'adapter_type', 'Unsupported C5 adapter.');
    json(response, 201, await adapters.start(id, sessionId, adapterType as C5AdapterType)); return true;
  }
  if (request.method === 'POST' && action === 'adapters/stop') { json(response, 200, adapters.stop(id, sessionId)); return true; }
  let op: number, payload: Buffer;
  if (request.method === 'PUT' && action === 'pins') {
    const protocol = integer(body.protocol, 1, 4, 'protocol');
    if (!Array.isArray(body.pins) || body.pins.length > 4) fail(400, 'invalid_pins', 'pins must contain at most four GPIO numbers.');
    payload = Buffer.alloc(12); payload[0] = protocol; payload[1] = body.pins.length;
    payload.writeUInt32LE(integer(body.hz, 0, 20000000, 'hz'), 4);
    for (let index = 0; index < 4; index++) payload.writeInt8(index < body.pins.length ? integer(body.pins[index], 0, 28, 'GPIO') : -1, 8 + index);
    op = OP.PINS;
  } else if (request.method === 'POST' && action === 'bus') {
    const data = hex(body.data ?? '', 1024);
    const protocol = integer(body.protocol, 2, 4, 'protocol');
    const address = integer(body.address ?? 0, 0, 127, 'address');
    const receive = integer(body.receive ?? 0, 0, 1024, 'receive');
    if ((!data.length && !receive) || (protocol === 4 ? address < 8 || address > 0x77 : address !== 0) ||
        (protocol === 3 && data.length && receive && data.length !== receive)) {
      fail(400, 'invalid_bus_transaction', 'Bus address or transfer lengths are invalid for the selected protocol.');
    }
    payload = Buffer.alloc(12 + data.length); payload[0] = protocol;
    payload.writeUInt16LE(address, 2); payload.writeUInt16LE(data.length, 4);
    payload.writeUInt16LE(receive, 6);
    payload.writeUInt16LE(integer(body.timeoutMs ?? 50, 1, 100, 'timeoutMs'), 8); data.copy(payload, 12); op = OP.BUS;
  } else if (request.method === 'POST' && action === 'dap') { payload = hex(body.data, 1024); op = OP.DAP; }
  else if (request.method === 'POST' && action === 'wifi/scan') { payload = Buffer.alloc(0); op = OP.WIFI_SCAN; }
  else if (request.method === 'POST' && ['ota/activate', 'ota/abort'].includes(action)) {
    const jobId = body.jobId;
    if (typeof jobId !== 'string' || !/^[0-9]{1,20}$/.test(jobId) || BigInt(jobId) > 0xffffffffffffffffn || BigInt(jobId) === 0n) {
      fail(400, 'invalid_job', 'jobId must be a nonzero unsigned 64-bit decimal string.');
    }
    payload = Buffer.alloc(8); payload.writeBigUInt64LE(BigInt(jobId)); op = action.endsWith('activate') ? OP.OTA_ACTIVATE : OP.OTA_ABORT;
  } else if (request.method === 'POST' && ['boot-confirm', 'reboot'].includes(action)) {
    payload = Buffer.alloc(0); op = action === 'reboot' ? OP.REBOOT : OP.BOOT_CONFIRM;
  } else fail(404, 'not_found', 'C5 endpoint was not found.');
  const started = performance.now();
  const data = await registry.command(id, sessionId, op, payload);
  if (op === OP.REBOOT || op === OP.OTA_ACTIVATE) {
    const record = registry.record(id), client = registry.client(record); record.session = null; record.connected = false; client.lease = 0n; client.close();
  }
  json(response, 200, { data: data.toString('hex'), byteLength: data.length, transactionMs: Math.round((performance.now() - started) * 100) / 100 });
  return true;
}
