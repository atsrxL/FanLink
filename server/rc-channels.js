import { EventEmitter } from 'node:events';
import { CHANNEL, FRAMETYPE, packChannels } from './crsf.js';

export const SAFE_CHANNEL_VALUES = Object.freeze([
  1500, 1500, 1000, 1500,
  1000, 1000, 1000, 1000,
  1000, 1000, 1000, 1000,
  1000, 1000, 1000, 1000,
]);

export const CENTER_CHANNEL_VALUES = Object.freeze(Array(CHANNEL.COUNT).fill(CHANNEL.US_MID));

function checkedValues(values) {
  if (!Array.isArray(values) || values.length !== CHANNEL.COUNT) {
    throw new TypeError(`通道值必须正好包含 ${CHANNEL.COUNT} 项`);
  }
  return values.map((value, index) => {
    const number = Number(value);
    if (!Number.isInteger(number) || number < CHANNEL.US_MIN || number > CHANNEL.US_MAX) {
      throw new RangeError(`CH${index + 1} 必须是 ${CHANNEL.US_MIN}–${CHANNEL.US_MAX} µs 的整数`);
    }
    return number;
  });
}

export class RcChannelController extends EventEmitter {
  constructor(link, { rateHz = 50, values = SAFE_CHANNEL_VALUES } = {}) {
    super();
    this.link = link;
    this.rateHz = Math.max(10, Math.min(150, Math.round(rateHz)));
    this.values = checkedValues([...values]);
    this.enabled = false;
    this.sentFrames = 0;
    this.lastSentAt = null;
    this.stopReason = null;
    this.timer = null;
  }

  setValues(values) {
    this.values = checkedValues(values);
    this.stopReason = null;
    this.emit('change', this.snapshot());
    return true;
  }

  reset(profile = 'safe') {
    return this.setValues([...(profile === 'center' ? CENTER_CHANNEL_VALUES : SAFE_CHANNEL_VALUES)]);
  }

  enable() {
    if (this.enabled) return true;
    if (typeof this.link?.sendStandardFrame !== 'function') return false;
    this.enabled = true;
    this.stopReason = null;
    if (!this.#send()) return false;
    this.timer = setInterval(() => this.#send(), Math.round(1000 / this.rateHz));
    this.emit('change', this.snapshot());
    return true;
  }

  disable(reason = null) {
    clearInterval(this.timer);
    this.timer = null;
    const changed = this.enabled || (reason && reason !== this.stopReason);
    this.enabled = false;
    this.stopReason = reason;
    if (changed) this.emit('change', this.snapshot());
  }

  snapshot() {
    return {
      enabled: this.enabled,
      values: [...this.values],
      rateHz: this.rateHz,
      sentFrames: this.sentFrames,
      lastSentAt: this.lastSentAt,
      stopReason: this.stopReason,
      min: CHANNEL.US_MIN,
      max: CHANNEL.US_MAX,
    };
  }

  #send() {
    if (!this.enabled) return false;
    const sent = this.link.sendStandardFrame(FRAMETYPE.RC_CHANNELS_PACKED, packChannels(this.values));
    if (!sent) {
      this.disable('串口写入失败，已自动停发');
      return false;
    }
    this.sentFrames++;
    this.lastSentAt = Date.now();
    if (this.sentFrames % this.rateHz === 0) this.emit('change', this.snapshot());
    return true;
  }
}
