import assert from 'node:assert/strict';
import { test } from 'node:test';
import { hexText, pinSelection, parseRoute } from '../apps/console/control-model.ts';

test('Console routes reject invalid encoding, unknown views and extra path components', () => {
  assert.deepEqual(parseRoute('#/devices/probe-1/pins'), { page: 'device', id: 'probe-1', view: 'pins' });
  assert.deepEqual(parseRoute(''), { page: 'devices' });
  for (const hash of ['#/devices/%zz', '#/devices/probe/x', '#/devices/probe/pins/x', '#/devices/%3Cscript%3E', '#/settings/x']) assert.equal(parseRoute(hash).page, 'not-found');
});
test('Pin selection rejects unavailable GPIO, duplicate pins, optional holes and protocol rate limits', () => {
  const mask = (1 << 2) | (1 << 3) | (1 << 4);
  assert.deepEqual(pinSelection(1, ['2', '3', ''], 1000000, mask), { protocol: 1, pins: [2, 3], hz: 1000000 });
  for (const values of [['2', '2', ''], ['2', '5', ''], ['2', '', '4']]) assert.throws(() => pinSelection(1, values, 1000000, mask));
  assert.throws(() => pinSelection(1, ['2', '3', ''], 4000001, mask));
  assert.throws(() => pinSelection(2, ['2', '3', '4', ''], 115200, mask));
});
test('Hex input accepts byte separators and rejects odd, malformed and oversized input', () => {
  assert.equal(hexText('01 02 AA'), '0102aa');
  for (const value of ['0', '01xx', 'ab'.repeat(1025)]) assert.throws(() => hexText(value));
});
