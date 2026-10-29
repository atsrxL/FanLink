import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { FRAMETYPE, unpackChannels } from '../server/crsf.js';
import { RcChannelController, SAFE_CHANNEL_VALUES } from '../server/rc-channels.js';
import { parseTelemetryFrame, TelemetryStore } from '../server/telemetry.js';

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

test('common CRSF telemetry payloads parse with ExpressLRS units', () => {
  const battery = parseTelemetryFrame({
    type: FRAMETYPE.BATTERY_SENSOR,
    payload: [0, 168, 0, 47, 0, 4, 210, 76],
  });
  assert.deepEqual(battery.values, {
    voltageV: 16.8,
    currentA: 4.7,
    capacityMah: 1234,
    remainingPercent: 76,
  });

  const link = parseTelemetryFrame({
    type: FRAMETYPE.LINK_STATISTICS,
    payload: [63, 67, 98, 0xf7, 0, 2, 3, 71, 94, 7],
  });
  assert.equal(link.values.uplinkRssi1Dbm, -63);
  assert.equal(link.values.uplinkLinkQuality, 98);
  assert.equal(link.values.uplinkSnrDb, -9);
  assert.equal(link.values.uplinkTxPowerMw, 100);

  const attitude = parseTelemetryFrame({
    type: FRAMETYPE.ATTITUDE,
    payload: [0x27, 0x10, 0xd8, 0xf0, 0, 0],
  });
  assert.equal(attitude.values.pitchDeg, 57.3);
  assert.equal(attitude.values.rollDeg, -57.3);

  const store = new TelemetryStore();
  assert.equal(store.accept({ type: FRAMETYPE.BATTERY_SENSOR, payload: [0, 168, 0, 47, 0, 4, 210, 76] }), true);
  assert.equal(store.accept({ type: FRAMETYPE.RC_CHANNELS_PACKED, payload: Buffer.alloc(22) }), false);
  assert.equal(store.snapshot().totalFrames, 1);
  assert.equal(store.snapshot().latest.battery.values.voltageV, 16.8);
});

test('RC output defaults off, sends all channels, and stops on link failure', async () => {
  const frames = [];
  const link = {
    working: true,
    sendStandardFrame(type, payload) {
      if (!this.working) return false;
      frames.push({ type, payload: Buffer.from(payload) });
      return true;
    },
  };
  const controller = new RcChannelController(link, { rateHz: 50 });
  assert.equal(controller.enabled, false);
  assert.deepEqual(controller.values, SAFE_CHANNEL_VALUES);
  await wait(30);
  assert.equal(frames.length, 0, 'disabled controller must not transmit');

  assert.equal(controller.enable(), true);
  await wait(35);
  assert.ok(frames.length >= 2);
  assert.equal(frames[0].type, FRAMETYPE.RC_CHANNELS_PACKED);
  assert.deepEqual(unpackChannels(frames[0].payload).slice(0, 5), [992, 992, 191, 992, 191]);

  const stopped = once(controller, 'change');
  link.working = false;
  await stopped;
  assert.equal(controller.enabled, false);
  assert.match(controller.stopReason, /写入失败/);
  const count = frames.length;
  await wait(30);
  assert.equal(frames.length, count);
});

test('RC output refuses to enable when the first serial write fails', () => {
  const controller = new RcChannelController({ sendStandardFrame: () => false });
  assert.equal(controller.enable(), false);
  assert.equal(controller.enabled, false);
  assert.equal(controller.timer, null);
  assert.match(controller.stopReason, /写入失败/);
});
