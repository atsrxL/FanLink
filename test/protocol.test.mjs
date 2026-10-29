import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { ElrsProtocol, encodeNumeric, parseField, splitOptions } from '../server/elrs-protocol.js';
import {
  buildStandardFrame,
  CHANNEL,
  CrsfDecoder,
  crsfToMicroseconds,
  FRAMETYPE,
  microsecondsToCrsf,
  packChannels,
  unpackChannels,
} from '../server/crsf.js';
import { SimHost } from '../server/sim-host.js';
import { displayedValue, valueScale, wireValue } from '../public/app-field-values.js';

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function cstr(value) {
  return [...Buffer.from(value, 'latin1'), 0];
}

async function within(promise, ms = 4000) {
  let timer;
  try {
    return await Promise.race([
      promise,
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error(`timed out after ${ms}ms`)), ms);
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}

function waitForField(protocol, id, predicate = () => true) {
  return within(new Promise((resolve) => {
    const handler = (field) => {
      if (field.id === id && predicate(field)) {
        protocol.off('field', handler);
        resolve(field);
      }
    };
    protocol.on('field', handler);
  }));
}

test('selection parsing preserves blank wire indexes', () => {
  assert.deepEqual(splitOptions('Off;;100mW;250mW;'), ['Off', '', '100mW', '250mW', '']);
  const wire = Buffer.from([
    0, 9, ...cstr('Power'), ...cstr('Off;;100mW;250mW'),
    2, 0, 3, 0, ...cstr('mW'),
  ]);
  const field = parseField(7, wire);
  assert.deepEqual(field.values, ['Off', '', '100mW', '250mW']);
  assert.equal(field.value, 2);
  assert.equal(field.values[field.value], '100mW');
});

test('signed 32-bit fields use four-byte two-complement values', () => {
  const wire = Buffer.from([
    0, 5, ...cstr('Offset'),
    0xff, 0xff, 0xff, 0xff,
    0x80, 0x00, 0x00, 0x00,
    0x7f, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00,
    ...cstr('us'),
  ]);
  const field = parseField(4, wire);
  assert.equal(field.value, -1);
  assert.equal(field.min, -2147483648);
  assert.equal(field.max, 2147483647);
  assert.deepEqual(encodeNumeric(-1, 5), [0xff, 0xff, 0xff, 0xff]);
  assert.deepEqual(encodeNumeric(-2147483648, 5), [0x80, 0x00, 0x00, 0x00]);
});

test('float fields edit user-facing values and write scaled integers', () => {
  const field = { type: 8, value: 123, min: -100, max: 1000, step: 5, prec: 100 };
  assert.equal(valueScale(field), 100);
  assert.equal(displayedValue(field), 1.23);
  assert.equal(wireValue(field, 2.5), 250);
  assert.equal(field.step / valueScale(field), 0.05);
});

test('standard CRSF frames decode and all 16 channels pack as 11-bit values', () => {
  const values = [1000, 1500, 2000, 1250, ...Array(12).fill(1500)];
  const packed = packChannels(values);
  assert.equal(packed.length, 22);
  const raw = unpackChannels(packed);
  assert.deepEqual(raw.slice(0, 4), [191, 992, 1792, microsecondsToCrsf(1250)]);
  assert.equal(crsfToMicroseconds(raw[0]), 1000);
  assert.equal(crsfToMicroseconds(raw[1]), 1500);
  assert.equal(crsfToMicroseconds(raw[2]), 2000);
  assert.equal(raw.length, CHANNEL.COUNT);

  const decoded = [];
  const decoder = new CrsfDecoder((frame) => decoded.push(frame));
  decoder.push(buildStandardFrame(FRAMETYPE.RC_CHANNELS_PACKED, packed));
  assert.equal(decoder.good, 1);
  assert.equal(decoded[0].type, FRAMETYPE.RC_CHANNELS_PACKED);
  assert.equal(decoded[0].dest, null);
  assert.deepEqual(decoded[0].payload, packed);
});

test('simulated protocol loads chunks, cancels old retries, and reads back saves', async () => {
  const host = new SimHost();
  const sent = [];
  const originalSend = host.sendFrame.bind(host);
  host.sendFrame = (...args) => {
    sent.push(args);
    return originalSend(...args);
  };
  const protocol = new ElrsProtocol(host);

  const online = once(host, 'state');
  await host.start();
  await within(online);
  const initialDone = once(protocol, 'load-done');
  host.ping();
  await within(initialDone);

  assert.equal(protocol.deviceList().length, 1);
  assert.equal(protocol.fields.size, 20);
  assert.deepEqual(protocol.fields.get(20).values, ['Off', '']);

  host.emit('frame', {
    type: FRAMETYPE.ELRS_STATUS,
    dest: 0xef,
    orig: 0xee,
    payload: [0, 0, 1, 0x04, 0],
  });
  assert.equal(protocol.linkStat.connectedBit, false);
  assert.equal(protocol.linkStat.modelMismatch, true);
  assert.equal(protocol.linkStat.armed, false);
  host.emit('frame', {
    type: FRAMETYPE.ELRS_STATUS,
    dest: 0xef,
    orig: 0xee,
    payload: [0, 0, 1, 0x08, 0],
  });
  assert.equal(protocol.linkStat.modelMismatch, false);
  assert.equal(protocol.linkStat.armed, true);

  const readsAfterLoad = sent.filter(([type]) => type === FRAMETYPE.PARAMETER_READ).length;
  await wait(1000);
  assert.equal(
    sent.filter(([type]) => type === FRAMETYPE.PARAMETER_READ).length,
    readsAfterLoad,
    'completed field timeouts must not retry later',
  );

  const saved = waitForField(protocol, 3, (field) => field.value === 0);
  assert.equal(protocol.saveValue(3, 0), true);
  await saved;
  assert.equal(protocol.fields.get(3).value, 0);
  assert.equal(protocol.saveValue(20, 1), false, 'blank selection slots are not writable');
  assert.equal(protocol.saveValue(10, 99), false, 'out-of-range numeric values are rejected');

  const confirm = waitForField(protocol, 16, (field) => field.status === 3);
  assert.equal(protocol.command(16, 1), true);
  await confirm;
  const running = waitForField(protocol, 16, (field) => field.status === 2);
  assert.equal(protocol.command(16, 4), true);
  await running;
  await waitForField(protocol, 16, (field) => field.status === 0);

  await host.stop();
});
