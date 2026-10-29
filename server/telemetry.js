import { EventEmitter } from 'node:events';
import { FRAMETYPE, signed8, signed16, signed32 } from './crsf.js';

const TYPE_NAMES = new Map([
  [FRAMETYPE.GPS, 'GPS'],
  [FRAMETYPE.GPS_TIME, 'GPS 时间'],
  [FRAMETYPE.VARIO, '升降率'],
  [FRAMETYPE.BATTERY_SENSOR, '电池'],
  [FRAMETYPE.BARO_ALTITUDE, '气压高度'],
  [FRAMETYPE.AIRSPEED, '空速'],
  [FRAMETYPE.HEARTBEAT, '心跳'],
  [FRAMETYPE.RPM, '转速'],
  [FRAMETYPE.TEMP, '温度'],
  [FRAMETYPE.CELLS, '电芯电压'],
  [FRAMETYPE.BAROMETER, '气压计'],
  [FRAMETYPE.MAGNETOMETER, '磁力计'],
  [FRAMETYPE.LINK_STATISTICS, '链路统计'],
  [FRAMETYPE.LINK_STATISTICS_RX, '接收链路统计'],
  [FRAMETYPE.LINK_STATISTICS_TX, '发送链路统计'],
  [FRAMETYPE.ATTITUDE, '姿态'],
  [FRAMETYPE.FLIGHT_MODE, '飞行模式'],
]);

const TX_POWER_MW = [0, 10, 25, 100, 500, 1000, 2000, 250];

function fixed(value, digits = 2) {
  return Number(value.toFixed(digits));
}

function u16(buf, offset = 0) {
  return (buf[offset] << 8) | buf[offset + 1];
}

function s16(buf, offset = 0) {
  return signed16(u16(buf, offset));
}

function u24(buf, offset = 0) {
  return (buf[offset] * 0x10000) + (buf[offset + 1] << 8) + buf[offset + 2];
}

function s24(buf, offset = 0) {
  const value = u24(buf, offset);
  return value & 0x800000 ? value - 0x1000000 : value;
}

function u32(buf, offset = 0) {
  return (
    (buf[offset] * 0x1000000) +
    ((buf[offset + 1] << 16) >>> 0) +
    (buf[offset + 2] << 8) +
    buf[offset + 3]
  );
}

function s32(buf, offset = 0) {
  return signed32(u32(buf, offset));
}

function text(buf) {
  const end = buf.indexOf(0);
  return buf.subarray(0, end < 0 ? buf.length : end).toString('utf8').trim();
}

function requireLength(payload, length) {
  return payload.length >= length;
}

function generic(frame, payload) {
  const typeHex = frame.type.toString(16).padStart(2, '0').toUpperCase();
  return {
    key: `raw-${typeHex}`,
    name: TYPE_NAMES.get(frame.type) || `CRSF 0x${typeHex}`,
    values: {},
    summary: `${payload.length} B`,
    known: false,
  };
}

export function parseTelemetryFrame(frame) {
  if (!frame || frame.type >= 0x28 || frame.type === FRAMETYPE.RC_CHANNELS_PACKED) return null;
  const p = Buffer.from(frame.payload || []);
  const fallback = () => generic(frame, p);

  switch (frame.type) {
    case FRAMETYPE.GPS: {
      if (!requireLength(p, 15)) return fallback();
      const values = {
        latitude: fixed(s32(p, 0) / 1e7, 7),
        longitude: fixed(s32(p, 4) / 1e7, 7),
        groundSpeedKmh: fixed(u16(p, 8) / 10, 1),
        headingDeg: fixed(u16(p, 10) / 100, 2),
        altitudeM: u16(p, 12) - 1000,
        satellites: p[14],
      };
      return {
        key: 'gps', name: 'GPS', values, known: true,
        summary: `${values.latitude.toFixed(5)}, ${values.longitude.toFixed(5)} · ${values.satellites} 星`,
      };
    }
    case FRAMETYPE.GPS_TIME: {
      if (!requireLength(p, 9)) return fallback();
      const values = {
        year: s16(p, 0), month: p[2], day: p[3], hour: p[4], minute: p[5], second: p[6], millisecond: u16(p, 7),
      };
      const pad = (value, width = 2) => String(value).padStart(width, '0');
      values.utc = `${pad(values.year, 4)}-${pad(values.month)}-${pad(values.day)} ${pad(values.hour)}:${pad(values.minute)}:${pad(values.second)}.${pad(values.millisecond, 3)} UTC`;
      return { key: 'gpsTime', name: 'GPS 时间', values, known: true, summary: values.utc };
    }
    case FRAMETYPE.VARIO: {
      if (!requireLength(p, 2)) return fallback();
      const values = { verticalSpeedMps: fixed(s16(p) / 100, 2) };
      return { key: 'vario', name: '升降率', values, known: true, summary: `${values.verticalSpeedMps.toFixed(2)} m/s` };
    }
    case FRAMETYPE.BATTERY_SENSOR: {
      if (!requireLength(p, 8)) return fallback();
      const values = {
        voltageV: fixed(u16(p, 0) / 10, 1),
        currentA: fixed(u16(p, 2) / 10, 1),
        capacityMah: u24(p, 4),
        remainingPercent: p[7],
      };
      return {
        key: 'battery', name: '电池', values, known: true,
        summary: `${values.voltageV.toFixed(1)} V · ${values.currentA.toFixed(1)} A · ${values.remainingPercent}%`,
      };
    }
    case FRAMETYPE.BARO_ALTITUDE: {
      if (!requireLength(p, 2)) return fallback();
      const raw = u16(p, 0);
      const altitudeM = raw & 0x8000 ? raw & 0x7fff : fixed(raw / 10 - 1000, 1);
      const values = { altitudeM };
      if (p.length >= 4) values.verticalSpeedMps = fixed(s16(p, 2) / 100, 2);
      return {
        key: 'baro', name: '气压高度', values, known: true,
        summary: `${altitudeM} m${values.verticalSpeedMps === undefined ? '' : ` · ${values.verticalSpeedMps.toFixed(2)} m/s`}`,
      };
    }
    case FRAMETYPE.AIRSPEED: {
      if (!requireLength(p, 2)) return fallback();
      const values = { airspeedKmh: fixed(u16(p, 0) / 10, 1) };
      return { key: 'airspeed', name: '空速', values, known: true, summary: `${values.airspeedKmh.toFixed(1)} km/h` };
    }
    case FRAMETYPE.HEARTBEAT: {
      if (!requireLength(p, 2)) return fallback();
      const values = { origin: u16(p, 0) };
      return { key: 'heartbeat', name: '心跳', values, known: true, summary: `来源 0x${values.origin.toString(16).toUpperCase()}` };
    }
    case FRAMETYPE.RPM: {
      if (!requireLength(p, 4)) return fallback();
      const values = { sourceId: p[0], rpm: [] };
      for (let offset = 1; offset + 2 < p.length; offset += 3) values.rpm.push(s24(p, offset));
      return { key: `rpm-${values.sourceId}`, name: `转速 ${values.sourceId}`, values, known: true, summary: values.rpm.map((v) => `${v} rpm`).join(' · ') };
    }
    case FRAMETYPE.TEMP: {
      if (!requireLength(p, 3)) return fallback();
      const values = { sourceId: p[0], temperaturesC: [] };
      for (let offset = 1; offset + 1 < p.length; offset += 2) values.temperaturesC.push(fixed(s16(p, offset) / 10, 1));
      return { key: `temperature-${values.sourceId}`, name: `温度 ${values.sourceId}`, values, known: true, summary: values.temperaturesC.map((v) => `${v.toFixed(1)} °C`).join(' · ') };
    }
    case FRAMETYPE.CELLS: {
      if (!requireLength(p, 3)) return fallback();
      const values = { sourceId: p[0], cellsV: [] };
      for (let offset = 1; offset + 1 < p.length; offset += 2) values.cellsV.push(fixed(u16(p, offset) / 1000, 3));
      return { key: `cells-${values.sourceId}`, name: `电芯 ${values.sourceId}`, values, known: true, summary: values.cellsV.map((v, i) => `${i + 1}:${v.toFixed(3)}V`).join(' · ') };
    }
    case FRAMETYPE.BAROMETER: {
      if (!requireLength(p, 8)) return fallback();
      const values = { pressurePa: s32(p, 0), temperatureC: fixed(s32(p, 4) / 100, 2) };
      return { key: 'barometer', name: '气压计', values, known: true, summary: `${values.pressurePa} Pa · ${values.temperatureC.toFixed(2)} °C` };
    }
    case FRAMETYPE.MAGNETOMETER: {
      if (!requireLength(p, 6)) return fallback();
      const values = { xMilligauss: s16(p, 0), yMilligauss: s16(p, 2), zMilligauss: s16(p, 4) };
      return { key: 'magnetometer', name: '磁力计', values, known: true, summary: `X ${values.xMilligauss} · Y ${values.yMilligauss} · Z ${values.zMilligauss} mG` };
    }
    case FRAMETYPE.LINK_STATISTICS: {
      if (!requireLength(p, 10)) return fallback();
      const values = {
        uplinkRssi1Dbm: -p[0],
        uplinkRssi2Dbm: -p[1],
        uplinkLinkQuality: p[2],
        uplinkSnrDb: signed8(p[3]),
        activeAntenna: p[4] + 1,
        rfMode: p[5],
        uplinkTxPowerMw: TX_POWER_MW[p[6]] ?? null,
        uplinkTxPowerCode: p[6],
        downlinkRssiDbm: -p[7],
        downlinkLinkQuality: p[8],
        downlinkSnrDb: signed8(p[9]),
      };
      if (p.length >= 11) values.downlinkRssi2Dbm = -p[10];
      return {
        key: 'link', name: '链路统计', values, known: true,
        summary: `上行 LQ ${values.uplinkLinkQuality}% / ${values.uplinkRssi1Dbm} dBm · 下行 LQ ${values.downlinkLinkQuality}%`,
      };
    }
    case FRAMETYPE.ATTITUDE: {
      if (!requireLength(p, 6)) return fallback();
      const toDegrees = (raw) => fixed((raw / 10000) * (180 / Math.PI), 1);
      const values = { pitchDeg: toDegrees(s16(p, 0)), rollDeg: toDegrees(s16(p, 2)), yawDeg: toDegrees(s16(p, 4)) };
      return { key: 'attitude', name: '姿态', values, known: true, summary: `P ${values.pitchDeg.toFixed(1)}° · R ${values.rollDeg.toFixed(1)}° · Y ${values.yawDeg.toFixed(1)}°` };
    }
    case FRAMETYPE.FLIGHT_MODE: {
      const values = { mode: text(p) || '—' };
      return { key: 'flightMode', name: '飞行模式', values, known: true, summary: values.mode };
    }
    default:
      return fallback();
  }
}

export class TelemetryStore extends EventEmitter {
  constructor({ recentLimit = 32, rateWindowMs = 5000 } = {}) {
    super();
    this.recentLimit = recentLimit;
    this.rateWindowMs = rateWindowMs;
    this.latest = new Map();
    this.recent = [];
    this.frameTimes = [];
    this.totalFrames = 0;
    this.lastFrameAt = null;
  }

  accept(frame) {
    const parsed = parseTelemetryFrame(frame);
    if (!parsed) return false;
    const now = Date.now();
    const payload = Buffer.from(frame.payload || []);
    const item = {
      ...parsed,
      type: frame.type,
      at: now,
      hex: payload.toString('hex').replace(/(..)/g, '$1 ').trim(),
    };
    this.totalFrames++;
    this.lastFrameAt = now;
    this.frameTimes.push(now);
    this.#trimTimes(now);
    this.latest.set(item.key, item);
    this.recent.unshift(item);
    if (this.recent.length > this.recentLimit) this.recent.length = this.recentLimit;
    this.emit('update', item);
    return true;
  }

  snapshot() {
    const now = Date.now();
    this.#trimTimes(now);
    const elapsed = this.frameTimes.length > 1 ? Math.max(now - this.frameTimes[0], 1000) : this.rateWindowMs;
    return {
      latest: Object.fromEntries(this.latest),
      recent: this.recent,
      totalFrames: this.totalFrames,
      lastFrameAt: this.lastFrameAt,
      rateHz: fixed((this.frameTimes.length * 1000) / elapsed, 1),
    };
  }

  #trimTimes(now) {
    const cutoff = now - this.rateWindowMs;
    while (this.frameTimes.length && this.frameTimes[0] < cutoff) this.frameTimes.shift();
  }
}
