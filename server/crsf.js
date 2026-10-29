// CRSF framing helpers (ExpressLRS / TBS CRSF).
// A wire frame is: sync(0xC8) len type [dest orig] payload crc
// len counts everything after the sync byte except itself (type..crc).

export const ADDR = Object.freeze({
  BROADCAST: 0x00,
  RADIO: 0xea, // EdgeTX/radio side address
  RX: 0xec,
  TX_MODULE: 0xee,
  ELRS_LUA: 0xef,
});

export const FRAMETYPE = Object.freeze({
  GPS: 0x02,
  GPS_TIME: 0x03,
  VARIO: 0x07,
  BATTERY_SENSOR: 0x08,
  BARO_ALTITUDE: 0x09,
  AIRSPEED: 0x0a,
  HEARTBEAT: 0x0b,
  RPM: 0x0c,
  TEMP: 0x0d,
  CELLS: 0x0e,
  BAROMETER: 0x11,
  MAGNETOMETER: 0x12,
  LINK_STATISTICS: 0x14,
  RC_CHANNELS_PACKED: 0x16,
  LINK_STATISTICS_RX: 0x1c,
  LINK_STATISTICS_TX: 0x1d,
  ATTITUDE: 0x1e,
  FLIGHT_MODE: 0x21,
  DEVICE_PING: 0x28,
  DEVICE_INFO: 0x29,
  PARAMETER_SETTINGS_ENTRY: 0x2b,
  PARAMETER_READ: 0x2c,
  PARAMETER_WRITE: 0x2d,
  ELRS_STATUS: 0x2e,
});

export function crc8(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) {
      crc = crc & 0x80 ? ((crc << 1) ^ 0xd5) & 0xff : (crc << 1) & 0xff;
    }
  }
  return crc;
}

// payload: array of bytes *after* dest/orig. Returns full wire frame Buffer.
export function buildFrame(type, dest, orig, payload = []) {
  const body = [type, dest, orig, ...payload];
  return Buffer.from([0xc8, body.length + 1, ...body, crc8(body)]);
}

// Broadcast/standard frame: sync len type payload crc (no destination/origin).
export function buildStandardFrame(type, payload = [], sync = 0xc8) {
  const body = [type, ...payload];
  return Buffer.from([sync, body.length + 1, ...body, crc8(body)]);
}

export const CHANNEL = Object.freeze({
  COUNT: 16,
  US_MIN: 1000,
  US_MID: 1500,
  US_MAX: 2000,
  CRSF_MIN: 191,
  CRSF_MID: 992,
  CRSF_MAX: 1792,
});

export function microsecondsToCrsf(value) {
  const us = Math.max(CHANNEL.US_MIN, Math.min(CHANNEL.US_MAX, Number(value)));
  if (us === CHANNEL.US_MID) return CHANNEL.CRSF_MID;
  return Math.round(
    CHANNEL.CRSF_MIN +
    ((us - CHANNEL.US_MIN) * (CHANNEL.CRSF_MAX - CHANNEL.CRSF_MIN)) /
      (CHANNEL.US_MAX - CHANNEL.US_MIN),
  );
}

export function crsfToMicroseconds(value) {
  const raw = Math.max(CHANNEL.CRSF_MIN, Math.min(CHANNEL.CRSF_MAX, Number(value)));
  if (raw === CHANNEL.CRSF_MID) return CHANNEL.US_MID;
  return Math.round(
    CHANNEL.US_MIN +
    ((raw - CHANNEL.CRSF_MIN) * (CHANNEL.US_MAX - CHANNEL.US_MIN)) /
      (CHANNEL.CRSF_MAX - CHANNEL.CRSF_MIN),
  );
}

export function packChannels(values) {
  if (!Array.isArray(values) || values.length !== CHANNEL.COUNT) {
    throw new TypeError(`expected exactly ${CHANNEL.COUNT} channel values`);
  }
  const output = Buffer.alloc(22);
  let accumulator = 0;
  let bitCount = 0;
  let offset = 0;
  for (const value of values) {
    accumulator |= (microsecondsToCrsf(value) & 0x7ff) << bitCount;
    bitCount += 11;
    while (bitCount >= 8) {
      output[offset++] = accumulator & 0xff;
      accumulator >>>= 8;
      bitCount -= 8;
    }
  }
  return output;
}

export function unpackChannels(payload) {
  const input = Buffer.from(payload);
  if (input.length < 22) throw new TypeError('packed channel payload must be 22 bytes');
  const values = [];
  let accumulator = 0;
  let bitCount = 0;
  let offset = 0;
  while (values.length < CHANNEL.COUNT) {
    while (bitCount < 11) {
      accumulator |= input[offset++] << bitCount;
      bitCount += 8;
    }
    values.push(accumulator & 0x7ff);
    accumulator >>>= 11;
    bitCount -= 11;
  }
  return values;
}

function findSync(buf) {
  for (let i = 0; i < buf.length; i++) {
    const b = buf[i];
    // Serial CRSF is always 0xC8; some endpoints emit their own address instead.
    if (b === 0xc8 || b === ADDR.TX_MODULE || b === ADDR.RADIO) return i;
  }
  return -1;
}

export class CrsfDecoder {
  constructor(onFrame) {
    this.buf = [];
    this.onFrame = onFrame;
    this.good = 0;
    this.bad = 0;
  }

  push(data) {
    for (const byte of data) this.#byte(byte);
  }

  #byte(byte) {
    this.buf.push(byte);
    let start = findSync(this.buf);
    if (start > 0) this.buf = this.buf.slice(start);
    if (this.buf.length < 3) return;
    if (this.buf[0] !== 0xc8 && this.buf[0] !== ADDR.TX_MODULE && this.buf[0] !== ADDR.RADIO) {
      this.buf.shift();
      return;
    }
    const len = this.buf[1];
    if (len < 2 || len > 62) {
      this.buf.shift();
      this.bad++;
      return;
    }
    const total = len + 2;
    if (this.buf.length < total) return;
    const frame = Buffer.from(this.buf.slice(0, total));
    this.buf = this.buf.slice(total);
    const crc = crc8(frame.subarray(2, total - 1));
    if (crc !== frame[total - 1]) {
      this.bad++;
      return;
    }
    this.good++;
    const type = frame[2];
    // Extended frames (0x28+) carry dest/orig; standard telemetry frames do not.
    if (type >= 0x28) {
      const parsed = {
        type,
        dest: frame[3],
        orig: frame[4],
        payload: frame.subarray(5, total - 1),
        raw: frame,
      };
      this.onFrame?.(parsed);
    } else {
      this.onFrame?.({
        type,
        dest: null,
        orig: null,
        payload: frame.subarray(3, total - 1),
        raw: frame,
      });
    }
  }
}

export function cstringAt(buf, offset) {
  let end = offset;
  while (end < buf.length && buf[end] !== 0) end++;
  const value = buf.subarray(offset, end).toString('latin1');
  return { value, next: Math.min(end + 1, buf.length) };
}

export function u32be(buf, offset) {
  return (
    (buf[offset] * 0x1000000) +
    ((buf[offset + 1] << 16) >>> 0) +
    (buf[offset + 2] << 8) +
    buf[offset + 3]
  );
}

export function u16be(buf, offset) {
  return (buf[offset] << 8) | buf[offset + 1];
}

export function signed32(v) {
  return v >= 0x80000000 ? v - 0x100000000 : v;
}

export function signed16(v) {
  return v >= 0x8000 ? v - 0x10000 : v;
}

export function signed8(v) {
  return v >= 0x80 ? v - 0x100 : v;
}
