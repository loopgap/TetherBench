function size(data: Buffer, offset: number, depth = 0): number {
  if (depth > 1 || offset >= data.length) throw new Error('Invalid CMSIS-DAP command.');
  const require = (count: number): number => { if (offset + count > data.length) throw new Error('Truncated CMSIS-DAP command.'); return count; };
  const command = data[offset];
  const fixed: Record<number, number> = { 0: 2, 1: 3, 2: 2, 3: 1, 4: 6, 8: 6, 9: 3, 10: 1, 16: 7, 17: 5, 19: 2 };
  if (Object.hasOwn(fixed, command)) return require(fixed[command]);
  if (command === 5) {
    require(3); let count = 3;
    for (let index = 0; index < data[offset + 2]; index++) { require(count + 1); const flags = data[offset + count++]; if (!(flags & 2) || flags & 0x10) count += 4; require(count); }
    return count;
  }
  if (command === 6) { require(5); return require(5 + ((data[offset + 4] & 2) ? 0 : data.readUInt16LE(offset + 2) * 4)); }
  if (command === 18) { require(2); return require(2 + Math.ceil((data[offset + 1] || 256) / 8)); }
  if (command === 29) {
    require(2); let count = 2;
    for (let index = 0; index < data[offset + 1]; index++) { require(count + 1); const info = data[offset + count++]; if (!(info & 128)) count += Math.ceil(((info & 63) || 64) / 8); require(count); }
    return count;
  }
  if (command === 127 && depth === 0) {
    require(2); const commands = data[offset + 1]; if (!commands || commands > 16) throw new Error('Invalid CMSIS-DAP command count.');
    let count = 2; for (let index = 0; index < commands; index++) count += size(data, offset + count, depth + 1); return count;
  }
  throw new Error('Unsupported CMSIS-DAP command.');
}

export function unpadDap(data: Buffer): Buffer {
  if (!Buffer.isBuffer(data) || !data.length || data.length > 1024) throw new Error('Invalid CMSIS-DAP packet length.');
  const count = size(data, 0);
  if (data.subarray(count).some((byte) => byte !== 0)) throw new Error('Nonzero CMSIS-DAP packet padding.');
  return data.subarray(0, count);
}
