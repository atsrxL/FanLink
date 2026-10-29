import { EventEmitter } from 'node:events';
import { ADDR, FRAMETYPE, unpackChannels } from './crsf.js';

// A deterministic in-memory TX module used to exercise the same wire protocol
// when no receiver is attached. It does not touch the serial port.

const F = (o) => ({
  hidden: false,
  unit: '',
  values: null,
  value: 0,
  min: 0,
  max: 0,
  status: 0,
  timeout: 200,
  info: '',
  ...o,
});

const demoFields = [
  F({ id: 1, parent: 0, type: 9, name: 'Packet Rate', values: ['250Hz(-108dBm)', '500Hz(-105dBm)', 'F1000', 'D250'], value: 1 }),
  F({ id: 2, parent: 0, type: 9, name: 'Telem Ratio', values: ['Off', '1:128', '1:64', '1:32'], value: 1 }),
  F({ id: 3, parent: 0, type: 9, name: 'Switch Mode', values: ['Hybrid', 'Wide'], value: 1 }),
  F({ id: 4, parent: 0, type: 11, name: 'TX Power' }),
  F({ id: 5, parent: 4, type: 9, name: 'Max Power', values: ['10', '25', '50', '100', '250', '500', '1000', '2000'], value: 2, unit: 'mW' }),
  F({ id: 6, parent: 4, type: 9, name: 'Dynamic', values: ['Off', 'Dyn', 'AUX9', 'AUX10'], value: 1 }),
  F({ id: 7, parent: 4, type: 9, name: 'Fan Thresh', values: ['10mW', '25mW', '50mW', '100mW', '250mW', '500mW', '1W', '2W', 'Never'], value: 6 }),
  F({ id: 8, parent: 0, type: 11, name: 'VTX Administrator' }),
  F({ id: 9, parent: 8, type: 9, name: 'Band/Enable', values: ['Disabled', 'A', 'B', 'E', 'F', 'R', 'L'], value: 0 }),
  F({ id: 10, parent: 8, type: 0, name: 'Channel', value: 1, min: 1, max: 8, unit: '' }),
  F({ id: 11, parent: 8, type: 9, name: 'Pwr Lvl', values: ['-', '1', '2', '3', '4', '5', '6', '7', '8'], value: 0 }),
  F({ id: 12, parent: 8, type: 9, name: 'Pitmode', values: ['Off', 'On'], value: 0 }),
  F({ id: 13, parent: 8, type: 13, name: 'Send VTx', status: 0 }),
  F({ id: 14, parent: 0, type: 12, name: 'Version', value: '3.5.2 (sim)' }),
  F({ id: 15, parent: 0, type: 11, name: 'WiFi Connectivity' }),
  F({ id: 16, parent: 15, type: 13, name: 'Enable WiFi', status: 0 }),
  F({ id: 17, parent: 15, type: 13, name: 'Enable Rx WiFi', status: 0 }),
  F({ id: 18, parent: 0, type: 13, name: 'Bind', status: 0 }),
  F({ id: 19, parent: 0, type: 13, name: 'BLE Joystick', status: 0 }),
  F({ id: 20, parent: 0, type: 9, name: 'Model Match', values: ['Off', ''], value: 0, unit: '(ID:1)' }),
];

const confirmIds = new Set([16, 17]);

function toBytes(value, width, signed) {
  let raw = signed && value < 0 ? value + (2 ** (width * 8)) : value;
  const out = new Array(width);
  for (let i = width - 1; i >= 0; i--) {
    out[i] = raw % 256;
    raw = Math.floor(raw / 256);
  }
  return out;
}

function cstr(str) {
  return [...Buffer.from(String(str), 'latin1'), 0];
}

function encodeWire(f) {
  const body = [f.parent || 0, (f.hidden ? 0x80 : 0) | f.type, ...cstr(f.name)];
  const type = f.type;
  if (type === 0 || type === 1) {
    body.push(...toBytes(f.value, 1, type === 1));
    body.push(...toBytes(f.min, 1, type === 1));
    body.push(...toBytes(f.max, 1, type === 1));
    body.push(0);
    body.push(...cstr(f.unit));
  } else if (type === 2 || type === 3) {
    body.push(...toBytes(f.value, 2, type === 3));
    body.push(...toBytes(f.min, 2, type === 3));
    body.push(...toBytes(f.max, 2, type === 3));
    body.push(0, 0);
    body.push(...cstr(f.unit));
  } else if (type === 9) {
    body.push(...cstr(f.values.join(';')));
    body.push(f.value);
    body.push(0);
    body.push(Math.max(f.values.length - 1, 0));
    body.push(0);
    body.push(...cstr(f.unit));
  } else if (type === 10 || type === 12) {
    body.push(...cstr(f.value));
  } else if (type === 13) {
    body.push(f.status, f.timeout, ...cstr(f.info));
  } else if (type === 11) {
    // folder only
  }
  return body;
}

function chunkPayload(f) {
  const data = Buffer.from(encodeWire(f));
  const chunkMax = 56;
  const chunks = [];
  for (let i = 0; i < data.length; i += chunkMax) {
    const piece = data.subarray(i, i + chunkMax);
    const remaining = Math.max(0, Math.ceil((data.length - i - piece.length) / chunkMax));
    chunks.push({ id: f.id, remaining, data: piece });
  }
  if (chunks.length === 0) chunks.push({ id: f.id, remaining: 0, data: Buffer.alloc(0) });
  return chunks;
}

function hex(list) {
  return [...list].map((b) => b.toString(16).padStart(2, '0')).join(' ');
}

export class SimHost extends EventEmitter {
  constructor() {
    super();
    this.path = 'simulator';
    this.fields = new Map(demoFields.map((f) => [f.id, structuredClone(f)]));
    this.linkGood = 42;
    this.linkBad = 1;
    this.simState = 'connecting';
    this.telemetryTimer = null;
    this.telemetryTick = 0;
    this.lastChannels = null;
  }

  async start() {
    setTimeout(() => {
      this.simState = 'online';
      this.emit('state', { mode: 'online', path: this.path, baud: 400000, sawValid: true });
      this.#log('模拟设备已就绪 (ExpressLRS TX, 演示模式)');
      this.#emitTelemetry();
      this.telemetryTimer = setInterval(() => this.#emitTelemetry(), 250);
      this.telemetryTimer.unref?.();
    }, 120);
  }

  async stop() {
    clearInterval(this.telemetryTimer);
    this.telemetryTimer = null;
    this.simState = 'closed';
    this.emit('state', { mode: 'closed', path: this.path });
  }

  sendFrame(type, dest, orig, payload = []) {
    if (this.simState !== 'online') return false;
    setTimeout(() => this.#handle(type, dest, orig, payload), 8);
    return true;
  }

  sendStandardFrame(type, payload = []) {
    if (this.simState !== 'online') return false;
    if (type === FRAMETYPE.RC_CHANNELS_PACKED) this.lastChannels = unpackChannels(payload);
    this.emit('tx', { type, dest: null, orig: null, payload: [...payload] });
    return true;
  }

  ping() {
    this.sendFrame(FRAMETYPE.DEVICE_PING, ADDR.BROADCAST, ADDR.RADIO);
  }

  #log(text) {
    this.emit('log', { level: 'debug', text });
  }

  #handle(type, dest, orig, payload) {
    this.#log(`TX ${type.toString(16).padStart(2, '0')} dest=${dest.toString(16)} ${hex(payload)}`);
    if (type === FRAMETYPE.DEVICE_PING) {
      const name = Buffer.from('ExpressLRS TX sim', 'latin1');
      const info = Buffer.concat([
        name,
        Buffer.from([0]),
        Buffer.from('ELRS', 'latin1'),
        Buffer.from([0, 0, 0, 0]), // hw
        Buffer.from([0, 0, 3, 5]), // sw (3.5 sim)
        Buffer.from([this.fields.size, 0]),
      ]);
      this.emit('frame', {
        type: FRAMETYPE.DEVICE_INFO,
        dest: orig,
        orig: ADDR.TX_MODULE,
        payload: info,
      });
      return;
    }
    if (type === FRAMETYPE.PARAMETER_READ) {
      const [fieldId, chunk] = payload;
      const field = this.fields.get(fieldId);
      if (!field) return;
      const chunks = chunkPayload(field);
      const wanted = Math.min(chunk, chunks.length - 1);
      const c = chunks[wanted];
      this.emit('frame', {
        type: FRAMETYPE.PARAMETER_SETTINGS_ENTRY,
        dest: orig,
        orig: dest,
        payload: [c.id, c.remaining, ...c.data],
      });
      return;
    }
    if (type === FRAMETYPE.PARAMETER_WRITE) {
      const fieldId = payload[0];
      const field = this.fields.get(fieldId);
      if (!field) {
        if (fieldId === 0 && payload[1] === 0) this.#linkstat(orig);
        return;
      }
      const action = payload[1];
      if (field.type === 13) {
        if (action === 1) {
          field.status = confirmIds.has(field.id) ? 3 : 2;
          field.info = confirmIds.has(field.id) ? 'Confirm enabling WiFi?' : 'Running...';
        } else if (action === 4) {
          field.status = 2;
          field.info = 'Running...';
        } else if (action === 5) {
          field.status = 0;
          field.info = 'Cancelled';
          this.#emitField(field, orig);
          return;
        }
        this.#emitField(field, orig);
        if (field.status === 2) {
          setTimeout(() => {
            field.status = 0;
            field.info = 'Done (sim)';
            this.#emitField(field, orig);
          }, 1400);
        }
        return;
      }
      if (field.type === 0 || field.type === 1) {
        field.value = field.type === 1 && payload[1] > 127 ? payload[1] - 256 : payload[1];
      } else if (field.type === 2 || field.type === 3) {
        field.value = (payload[1] << 8) | payload[2];
        if (field.type === 3 && field.value > 0x7fff) field.value -= 0x10000;
      } else if (field.type === 9) {
        field.value = Math.min(payload[1] ?? 0, field.values.length - 1);
      }
      this.#emitField(field, orig);
      this.#log(`参数 ${field.name} 已保存 = ${field.value}`);
    }
  }

  #linkstat(orig) {
    const payload = [
      this.linkBad & 0xff,
      (this.linkGood >> 8) & 0xff,
      this.linkGood & 0xff,
      0x01,
      0,
    ];
    this.emit('frame', { type: FRAMETYPE.ELRS_STATUS, dest: orig, orig: ADDR.TX_MODULE, payload });
  }

  #emitField(field, orig) {
    const chunks = chunkPayload(field);
    for (const c of chunks) {
      this.emit('frame', {
        type: FRAMETYPE.PARAMETER_SETTINGS_ENTRY,
        dest: orig,
        orig: ADDR.TX_MODULE,
        payload: [c.id, c.remaining, ...c.data],
      });
    }
  }

  #emitTelemetry() {
    if (this.simState !== 'online') return;
    this.telemetryTick++;
    const wave = Math.sin(this.telemetryTick / 8);
    const int16 = (value) => {
      const raw = value < 0 ? value + 0x10000 : value;
      return [(raw >> 8) & 0xff, raw & 0xff];
    };
    this.emit('frame', {
      type: FRAMETYPE.ATTITUDE,
      dest: null,
      orig: null,
      payload: [...int16(Math.round(wave * 1200)), ...int16(Math.round(wave * 2200)), ...int16(Math.round(this.telemetryTick * 80))],
    });
    this.emit('frame', {
      type: FRAMETYPE.LINK_STATISTICS,
      dest: null,
      orig: null,
      payload: [63, 67, 98, 9, 0, 2, 3, 71, 94, 7],
    });
    if (this.telemetryTick % 4 === 1) {
      this.emit('frame', {
        type: FRAMETYPE.BATTERY_SENSOR,
        dest: null,
        orig: null,
        payload: [0, 168, 0, 47, 0, 4, 210, 76],
      });
      this.emit('frame', {
        type: FRAMETYPE.GPS,
        dest: null,
        orig: null,
        payload: [13, 157, 240, 122, 69, 17, 66, 188, 0, 126, 35, 40, 3, 244, 17],
      });
    }
    if (this.telemetryTick % 8 === 1) {
      this.emit('frame', {
        type: FRAMETYPE.FLIGHT_MODE,
        dest: null,
        orig: null,
        payload: Buffer.from('ANGLE\0'),
      });
    }
  }
}
