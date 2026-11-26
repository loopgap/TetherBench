import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { startDapTcp } from '../../../apps/gateway/c5-adapters.ts';
import type { DapTcpServer } from '../../../apps/gateway/c5-adapters.ts';

interface ParserRequest { resolve(value: Buffer): void; reject(reason?: unknown): void; timer: ReturnType<typeof setTimeout> }

const root = fileURLToPath(new URL('../', import.meta.url));
const openocd = process.env.OPENOCD;
if (!openocd) throw new Error('Set OPENOCD to the installed executable; no backend check was run.');
const folder = await mkdtemp(join(tmpdir(), 'tetherbench-debug-'));
let parser: ChildProcess | undefined, adapter: DapTcpServer | undefined;
const pending: { current: ParserRequest | null } = { current: null };
try {
  const executable = join(folder, process.platform === 'win32' ? 'dap_stdio.exe' : 'dap_stdio');
  const compile = spawnSync(process.env.CC ?? 'gcc', ['-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter', '-DTB_HOST_TEST',
    '-I', resolve(root, 'main'), '-I', resolve(root, 'tests'), '-I', resolve(root, 'third_party/CMSIS-DAP/Include'),
    ...['main/probe_core.c', 'third_party/CMSIS-DAP/Source/DAP.c', 'third_party/CMSIS-DAP/Source/SW_DP.c', 'tests/dap_stdio.c'].map((path) => resolve(root, path)), '-o', executable],
  { encoding: 'utf8', timeout: 30000, windowsHide: true });
  assert.equal(compile.error, undefined, compile.error?.message);
  assert.equal(compile.status, 0, compile.stderr);
  parser = spawn(executable, [], { windowsHide: true });
  let bytes = Buffer.alloc(0), parserError = '';
  const commands: string[] = [];
  parser.stderr?.on('data', (chunk: Buffer) => { parserError = (parserError + chunk.toString('utf8')).slice(-4096); });
  parser.on('error', (error: Error) => pending.current?.reject(error));
  parser.on('exit', (code: number | null) => pending.current?.reject(new Error(parserError || 'DAP parser exited: ' + code)));
  parser.stdin?.on('error', (error: Error) => pending.current?.reject(error));
  parser.stdout?.on('data', (chunk: Buffer) => {
    bytes = Buffer.concat([bytes, chunk]);
    if (bytes.length >= 2 && bytes.length >= 2 + bytes.readUInt16LE()) {
      const result = Buffer.from(bytes.subarray(2, 2 + bytes.readUInt16LE())); bytes = bytes.subarray(2 + result.length);
      const current = pending.current; pending.current = null;
      if (current) { clearTimeout(current.timer); current.resolve(result); }
    }
  });
  adapter = await startDapTcp((packet: Buffer) => new Promise<Buffer>((resolvePromise, reject) => {
    commands.push(packet.toString('hex'));
    pending.current = { resolve: resolvePromise, reject, timer: setTimeout(() => reject(new Error(parserError || 'Host parser timed out')), 2000) };
    const header = Buffer.alloc(2); header.writeUInt16LE(packet.length);
    parser?.stdin?.write(Buffer.concat([header, packet]));
  }));
  const configuration = ['adapter driver cmsis-dap', 'cmsis-dap backend tcp', 'cmsis-dap tcp host 127.0.0.1',
    'cmsis-dap tcp port ' + adapter.port, 'cmsis-dap tcp min_timeout 150', 'transport select swd', 'adapter speed 1000',
    'init', 'cmsis-dap info', 'shutdown'];
  const output = await new Promise<string>((resolvePromise, reject) => {
    const backend = spawn(openocd, configuration.flatMap((line) => ['-c', line]), { windowsHide: true });
    let text = '';
    backend.stdout?.on('data', (chunk: Buffer) => { text = (text + chunk.toString('utf8')).slice(-32768); });
    backend.stderr?.on('data', (chunk: Buffer) => { text = (text + chunk.toString('utf8')).slice(-32768); });
    const timer = setTimeout(() => { backend.kill(); reject(new Error('OpenOCD exceeded 15 seconds: ' + text + parserError + '\nCommands: ' + commands.join(', '))); }, 15000);
    backend.on('error', (error: Error) => { clearTimeout(timer); reject(error); });
    backend.on('exit', (code: number | null) => { clearTimeout(timer); code === 0 ? resolvePromise(text) : reject(new Error('OpenOCD exit ' + code + ': ' + text + parserError)); });
  });
  for (const prefix of ['00f0', '00ff', '02', '11', '04']) assert.ok(commands.some((packet) => packet.startsWith(prefix)), 'Missing initialization command ' + prefix);
  assert.match(output, /CMSIS-DAP.*SWD/i);
  const lines = output.split(/\r?\n/);
  const socketWarnings = lines.filter((line) => line.includes('WSAGetLastError==10035'));
  const emptyReads = lines.filter((line) => line.includes('CMSIS-DAP: error reading header'));
  const unexpectedErrors = lines.filter((line) => line.startsWith('Error:')
    && !line.includes('CMSIS-DAP: error reading header')
    && !line.includes('WSAGetLastError==10035'));
  assert.equal(emptyReads.length, socketWarnings.length, 'Unexpected OpenOCD TCP polling errors: ' + output);
  assert.equal(unexpectedErrors.length, 0, 'Unexpected OpenOCD errors: ' + output);
  console.log(lines.filter((line) => /Open On-Chip Debugger|Interface Initialised|Interface ready|shutdown command invoked/.test(line)).join('\n'));
  if (socketWarnings.length) console.log('Observed ' + socketWarnings.length + ' Windows WSA 10035 empty-socket polling warnings from this OpenOCD build.');
  console.log('PASS: installed OpenOCD initialized through the TCP relay and the pinned Arm parser (' + commands.length + ' commands). GPIO is stubbed; no C5 radio or target was tested.');
} finally {
  if (pending.current) clearTimeout(pending.current.timer);
  adapter?.close(); parser?.kill();
  // The executable can remain locked briefly on Windows after termination.
  await rm(folder, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
}
