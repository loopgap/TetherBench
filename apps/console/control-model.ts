export const VIEWS = Object.freeze({ overview: '概览', pins: '引脚配置', terminal: '协议事务', debug: '调试后端', ota: '固件更新', wifi: '无线链路' } as const);
export type DeviceView = keyof typeof VIEWS;
export type ProtocolId = 1 | 2 | 3 | 4;

export const PROTOCOLS = Object.freeze({
  1: { name: 'SWD', roles: ['SWCLK', 'SWDIO', 'nRESET（可选）'], minimum: 2, maximum: 4000000, hz: 1000000 },
  2: { name: 'UART', roles: ['TX', 'RX', 'RTS（可选）', 'CTS（可选）'], minimum: 2, maximum: 5000000, hz: 115200 },
  3: { name: 'SPI', roles: ['SCLK', 'MOSI', 'MISO', 'CS'], minimum: 4, maximum: 20000000, hz: 1000000 },
  4: { name: 'I2C', roles: ['SCL', 'SDA'], minimum: 2, maximum: 1000000, hz: 400000 },
} as const satisfies Record<ProtocolId, { name: string; roles: readonly string[]; minimum: number; maximum: number; hz: number }>);

export type ConsolePage = 'devices' | 'sessions' | 'activity' | 'settings' | 'simulation';
export type Route = { page: ConsolePage | 'not-found'; id?: string; view?: DeviceView } | { page: 'device'; id: string; view: DeviceView };

export function parseRoute(hash: string): Route {
  try {
    const parts = (hash || '#/devices').replace(/^#/, '').split('/').filter(Boolean).map(decodeURIComponent);
    if (parts.length === 1 && ['devices', 'sessions', 'activity', 'settings', 'simulation'].includes(parts[0])) return { page: parts[0] as ConsolePage };
    const view = parts[2] ?? 'overview';
    if (parts[0] === 'devices' && parts.length >= 2 && parts.length <= 3
      && /^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$/.test(parts[1]) && Object.hasOwn(VIEWS, view)) {
      return { page: 'device', id: parts[1], view: view as DeviceView };
    }
  } catch { /* Invalid URL encoding belongs to the not-found view. */ }
  return { page: 'not-found' };
}

export function hexText(value: FormDataEntryValue | string | null, maximum = 1024): string {
  if (typeof value !== 'string' || value.length > maximum * 4) throw new Error('输入超过事务长度限制。');
  const text = [...value].filter((character) => character.charCodeAt(0) > 32).join('');
  if (text.length > maximum * 2 || !/^(?:[0-9a-fA-F]{2})*$/.test(text)) throw new Error('请输入完整的十六进制字节，可使用空格分隔。');
  return text.toLowerCase();
}

export interface PinSelection { protocol: ProtocolId; pins: number[]; hz: number }
export function pinSelection(protocol: number, values: readonly (FormDataEntryValue | null)[], hz: number, mask: number): PinSelection {
  if (!Number.isInteger(protocol) || !Object.hasOwn(PROTOCOLS, protocol)) throw new Error('不支持该协议。');
  const protocolId = protocol as ProtocolId;
  const spec = PROTOCOLS[protocolId];
  if (!Number.isInteger(hz) || hz < (protocolId === 1 ? 1000 : 1) || hz > spec.maximum) throw new Error('时钟或波特率超出该协议范围。');
  const pins = values.map((value) => value === '' ? -1 : Number(value));
  while (pins.at(-1) === -1) pins.pop();
  if (pins.length < spec.minimum || pins.length > spec.roles.length || (protocolId === 2 && ![2, 4].includes(pins.length))) throw new Error('请配置完整的信号引脚；UART 硬件流控需要同时配置 RTS 和 CTS。');
  if (pins.some((pin) => !Number.isInteger(pin) || pin < 0 || pin > 28 || !(mask & (1 << pin))) || new Set(pins).size !== pins.length) {
    throw new Error('请选择可用且互不重复的 GPIO。');
  }
  return { protocol: protocolId, pins, hz };
}

export const OTA_PHASES = ['空闲', '接收中', '已校验，待激活', '已激活，待确认', '已确认', '失败'] as const;
