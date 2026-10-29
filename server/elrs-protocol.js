import { EventEmitter } from 'node:events';
import {
  ADDR,
  FRAMETYPE,
  cstringAt,
  u16be,
  u32be,
  signed8,
  signed16,
  signed32,
} from './crsf.js';

export const TYPE_NAMES = {
  0: 'UINT8', 1: 'INT8', 2: 'UINT16', 3: 'INT16', 4: 'UINT32', 5: 'INT32',
  8: 'FLOAT', 9: '选择', 10: 'STRING', 11: '文件夹', 12: 'INFO', 13: 'COMMAND',
};

export const CMD_STATUS = {
  0: '空闲/完成', 1: '已点击', 2: '执行中', 3: '等待确认', 4: '已确认', 5: '取消中', 6: '查询中',
};

function numericWidth(type) {
  if (type <= 1) return 1;
  if (type <= 3) return 2;
  if (type <= 5) return 4;
  throw new Error(`not numeric: ${type}`);
}

function readUint(buf, offset, width) {
  let v = 0;
  for (let i = 0; i < width; i++) v = v * 256 + buf[offset + i];
  return v;
}

function toSigned(v, width) {
  const modulus = 2 ** (width * 8);
  const signBit = modulus / 2;
  return v >= signBit ? v - modulus : v;
}

export function splitOptions(str) {
  return str
    .split(';')
    .map((item) => item.replace(/\xc0/g, '↑').replace(/\xc1/g, '↓'));
}

export function parseField(id, data) {
  if (data.length < 2) return null;
  const parent = data[0] === 0 ? null : data[0];
  const rawType = data[1];
  const type = rawType & 0x7f;
  const hidden = (rawType & 0x80) !== 0;
  let pos = 2;
  const nameInfo = cstringAt(data, pos);
  pos = nameInfo.next;
  const f = {
    id, parent, type, hidden, name: nameInfo.value, unit: '', value: null,
    min: null, max: null, values: null, info: '', status: null, timeout: 0,
    step: null, prec: null, loaded: true,
  };

  if (type >= 0 && type <= 5) {
    const width = numericWidth(type);
    const signed = type % 2 === 1;
    const value = readUint(data, pos, width);
    const min = readUint(data, pos + width, width);
    const max = readUint(data, pos + 2 * width, width);
    const unitInfo = cstringAt(data, pos + 4 * width); // value/min/max/default
    f.value = signed ? toSigned(value, width) : value;
    f.min = signed ? toSigned(min, width) : min;
    f.max = signed ? toSigned(max, width) : max;
    f.unit = unitInfo.value;
    f.step = 1;
  } else if (type === 8) {
    // FLOAT: value/min/max/default (4 bytes each), precision byte, uint32 step, unit.
    f.value = signed32(u32be(data, pos));
    f.min = signed32(u32be(data, pos + 4));
    f.max = signed32(u32be(data, pos + 8));
    const precision = data[pos + 16] ?? 0;
    f.step = u32be(data, pos + 17);
    f.prec = Math.pow(10, Math.min(precision, 3));
    const unitInfo = cstringAt(data, pos + 21);
    f.unit = unitInfo.value;
  } else if (type === 9) {
    const optionsInfo = cstringAt(data, pos);
    f.values = splitOptions(optionsInfo.value);
    pos = optionsInfo.next;
    f.value = data[pos] ?? 0;
    // value, min, max, default then unit string
    const unitInfo = cstringAt(data, pos + 4);
    f.unit = unitInfo.value;
  } else if (type === 10 || type === 12) {
    const valueInfo = cstringAt(data, pos);
    f.value = valueInfo.value;
    f.info = f.value;
  } else if (type === 11) {
    // folder: children are implied by parent ids in the field stream
  } else if (type === 13) {
    f.status = data[pos] ?? 0;
    f.timeout = data[pos + 1] ?? 0;
    const infoInfo = cstringAt(data, pos + 2);
    f.info = infoInfo.value;
  }
  return f;
}

function encodeBytes(value, width) {
  const out = new Array(width);
  let rest = value;
  for (let i = width - 1; i >= 0; i--) {
    out[i] = rest % 256;
    rest = Math.floor(rest / 256);
  }
  return out;
}

export function encodeNumeric(value, type) {
  const width = numericWidth(type);
  const signed = type % 2 === 1;
  const raw = signed && value < 0 ? value + (2 ** (width * 8)) : value;
  return encodeBytes(raw, width);
}

function encodeFloat(value) {
  const raw = value < 0 ? value + 0x100000000 : value;
  return encodeBytes(raw, 4);
}

// Reproduces elrs.lua field loading semantics over the serial CRSF host.
export class ElrsProtocol extends EventEmitter {
  constructor(link) {
    super();
    this.link = link;
    this.devices = new Map();
    this.targetId = ADDR.TX_MODULE;
    this.fields = new Map();
    this.linkStat = null;
    this.loading = false;
    this.queue = [];
    this.current = null;
    this.partials = new Map();
    this.timers = new Set();
    this.reloadRequested = false;
    this.total = 0;
    this.done = 0;
    this.queryTimers = new Map();
    this.commandPending = new Set();
    this.linkstatTimer = null;
    link.on('frame', (frame) => this.onFrame(frame));
    link.on('tx', ({ type, payload }) => {
      if (type === FRAMETYPE.PARAMETER_READ) this.emit('log', { level: 'debug', text: `读取参数 id=${payload[0]} chunk=${payload[1]}` });
      if (type === FRAMETYPE.PARAMETER_WRITE && payload[0] !== 0) {
        this.emit('log', { level: 'debug', text: `写入参数 id=${payload[0]} 值=[${payload.slice(1).join(',')}]` });
      }
    });
    link.on('state', (state) => {
      if (state.mode !== 'online') {
        this.devices.clear();
        this.fields.clear();
        this.linkStat = null;
        this.loading = false;
        this.#clearAllTimers();
        clearInterval(this.linkstatTimer);
        this.linkstatTimer = null;
      } else if (!this.linkstatTimer) {
        this.linkstatTimer = setInterval(() => {
          // elrs.lua asks the ELRS TX module for its bad/good packet status.
          this.link.sendFrame(FRAMETYPE.PARAMETER_WRITE, ADDR.TX_MODULE, ADDR.ELRS_LUA, [0, 0]);
        }, 1200);
      }
    });
  }

  targetDevice() {
    return this.devices.get(this.targetId) || null;
  }

  originFor(id = this.targetId) {
    const device = this.devices.get(id);
    // ELRS Lua talks to the internal ELRS TX module as 0xEF; anything reached
    // through the module (Other Devices) is queried as a normal radio (0xEA).
    return device?.isElrs && id === ADDR.TX_MODULE ? ADDR.ELRS_LUA : ADDR.RADIO;
  }

  deviceList() {
    return [...this.devices.values()];
  }

  selectTarget(id) {
    if (!this.devices.has(id)) return false;
    this.targetId = id;
    this.emit('device-list');
    this.reloadAll();
    return true;
  }

  reloadAll() {
    const device = this.targetDevice();
    if (!device) return false;
    this.#clearAllTimers();
    this.fields.clear();
    this.emit('load-reset');
    this.queue = [];
    this.partials.clear();
    this.done = 0;
    this.total = device.fldcnt;
    this.emit('load-start', { total: this.total });
    for (let id = 1; id <= device.fldcnt; id++) this.queue.push(id);
    this.loading = true;
    this.#afterDelay(40, () => this.#pump());
    return true;
  }

  reloadFields(ids) {
    const device = this.targetDevice();
    if (!device) return false;
    const wanted = [...new Set(ids.map(Number))]
      .filter((id) => Number.isInteger(id) && id >= 1 && id <= device.fldcnt);
    if (!wanted.length) return false;

    const wasLoading = this.loading;
    if (!wasLoading) {
      this.current = null;
      this.queue = [];
      for (const key of this.partials.keys()) {
        if (key.startsWith('read:')) this.partials.delete(key);
      }
      this.done = 0;
      this.total = 0;
      this.loading = true;
    }

    const added = [];
    for (const id of wanted) {
      this.fields.delete(id);
      const alreadyQueued = this.queue.includes(id);
      const readingNow = this.current?.id === id && this.current?.target === device.id;
      if (!alreadyQueued && !readingNow) added.push(id);
      else if (readingNow && !alreadyQueued) added.push(id); // read it once more after the active response
    }
    if (!added.length) return true;
    this.queue = [...added, ...this.queue];
    this.total += added.length;
    if (!wasLoading) this.emit('load-start', { total: this.total, done: this.done });
    else this.emit('progress', { done: this.done, total: this.total });
    if (!this.current) this.#pump();
    return true;
  }

  onFrame(frame) {
    switch (frame.type) {
      case FRAMETYPE.DEVICE_INFO:
        this.#onDeviceInfo(frame);
        break;
      case FRAMETYPE.PARAMETER_SETTINGS_ENTRY:
        this.#onParameterEntry(frame);
        break;
      case FRAMETYPE.ELRS_STATUS:
        this.#onLinkStat(frame);
        break;
    }
  }

  #onDeviceInfo(frame) {
    const id = frame.orig;
    const payload = Buffer.from(frame.payload);
    if (payload.length < 18) return;
    const nameInfo = cstringAt(payload, 0);
    const serial = u32be(payload, nameInfo.next);
    const hw = u32be(payload, nameInfo.next + 4);
    const sw = u32be(payload, nameInfo.next + 8);
    const fldcnt = payload[nameInfo.next + 12];
    const pver = payload[nameInfo.next + 13];
    const prev = this.devices.get(id);
    const device = {
      id,
      name: nameInfo.value,
      serial,
      isElrs: serial === 0x454c5253,
      hw,
      sw,
      fldcnt,
      parameterVersion: pver,
    };
    this.devices.set(id, device);
    this.emit('device-list');
    if (!prev || prev.name !== device.name || prev.fldcnt !== device.fldcnt) {
      this.emit('log', {
        level: 'info',
        text: `发现设备 0x${id.toString(16).toUpperCase().padStart(2, '0')} ${device.name} (${device.isElrs ? 'ELRS' : '其他'}，${fldcnt} 项参数)`,
      });
    }
    const firstSane = this.devices.has(ADDR.TX_MODULE) ? ADDR.TX_MODULE : id;
    if (!this.devices.has(this.targetId)) this.targetId = firstSane;
    if (this.targetId === id && (!prev || prev.fldcnt !== device.fldcnt)) {
      this.reloadAll();
    }
  }

  #onLinkStat(frame) {
    const payload = Buffer.from(frame.payload);
    if (payload.length < 4) return;
    const bad = payload[0];
    const good = (payload[1] << 8) | payload[2];
    const flags = payload[3];
    const msgInfo = cstringAt(payload, 4);
    this.linkStat = {
      bad,
      good,
      flags,
      msg: msgInfo.value,
      connectedBit: (flags & 0x01) !== 0,
      modelMismatch: (flags & 0x04) !== 0,
      armed: (flags & 0x08) !== 0,
    };
    this.emit('linkstat', this.linkStat);
  }

  #onParameterEntry(frame) {
    const payload = Buffer.from(frame.payload);
    if (payload.length < 3) return;
    const id = payload[0];
    const remain = payload[1];
    const chunk = payload.subarray(2);
    const expectedOrigin = this.originFor();
    if (frame.dest !== expectedOrigin || frame.orig !== this.targetId) return;

    const isRead = this.current?.id === id
      && this.current.target === frame.orig
      && this.current.origin === frame.dest;
    const isCommand = this.commandPending.has(id);

    // Accept a complete unsolicited update, but never assemble unknown chunks;
    // otherwise a late response can be mistaken for the selected device's load.
    if (!isRead && !isCommand) {
      if (remain !== 0) return;
      const field = parseField(id, chunk);
      if (field) this.#storeField(field);
      return;
    }

    const partialKey = `${isRead ? 'read' : 'command'}:${frame.orig}:${id}`;
    let partial = this.partials.get(partialKey);
    if (!partial) {
      partial = { chunks: [], expectedRemain: null };
      this.partials.set(partialKey, partial);
    } else if (partial.expectedRemain !== remain) {
      this.emit('log', {
        level: 'warn',
        text: `参数 ${id} 分块顺序异常：期望 remain=${partial.expectedRemain}，收到 ${remain}`,
      });
      return;
    }

    if (isRead) this.#clearCurrentTimer();
    partial.chunks.push(...chunk);
    if (remain > 0) {
      partial.expectedRemain = remain - 1;
      if (isRead) {
        this.current.chunk++;
        this.current.attempts = 0;
        this.#afterDelay(25, () => {
          if (this.current?.id === id && this.current?.target === frame.orig) this.#sendCurrentRead();
        });
      } else {
        // Command responses request subsequent chunks with lcsQuery (action 6).
        this.#afterDelay(25, () => {
          if (this.commandPending.has(id)) this.#sendCommandAction(id, 6, false);
        });
      }
      return;
    }

    this.partials.delete(partialKey);
    if (isCommand) this.commandPending.delete(id);
    const field = parseField(id, Buffer.from(partial.chunks));
    if (field) this.#storeField(field);

    // Finish the active read if this was it.
    if (isRead && this.current?.id === id && this.current?.target === frame.orig) {
      this.current = null;
      this.done++;
      this.emit('progress', { done: this.done, total: this.total });
      this.#afterDelay(18, () => this.#pump());
    }
  }

  #storeField(field) {
    this.fields.set(field.id, field);
    this.emit('field', field);
    this.#manageCommand(field);
  }

  #pump() {
    if (!this.loading || this.current) return;
    const id = this.queue.shift();
    if (id === undefined) {
      this.loading = false;
      this.emit('load-done');
      return;
    }
    this.#startRead(id, 0);
  }

  #startRead(id, chunk) {
    const device = this.targetDevice();
    if (!device) return;
    this.current = {
      id,
      target: device.id,
      origin: this.originFor(device.id),
      chunk,
      attempts: 0,
      timer: null,
    };
    this.partials.delete(`read:${device.id}:${id}`);
    this.#sendCurrentRead();
  }

  sendRead(id, chunk) {
    if (!this.current || this.current.id !== id) return false;
    this.current.chunk = chunk;
    return this.#sendCurrentRead();
  }

  #sendCurrentRead() {
    const current = this.current;
    if (!current) return false;
    this.#clearCurrentTimer();
    const sent = this.link.sendFrame(
      FRAMETYPE.PARAMETER_READ,
      current.target,
      current.origin,
      [current.id, current.chunk],
    );
    const remote = current.target !== ADDR.TX_MODULE;
    const timeout = remote ? 3500 : 900;
    let timer;
    timer = this.#afterDelay(timeout, () => {
      if (this.current !== current || current.timer !== timer) return;
      current.timer = null;
      current.attempts++;
      if (current.attempts >= 3) {
        this.emit('log', { level: 'warn', text: `参数 ${current.id} 无响应，跳过` });
        this.partials.delete(`read:${current.target}:${current.id}`);
        this.current = null;
        this.done++;
        this.emit('progress', { done: this.done, total: this.total });
        this.#pump();
      } else {
        this.#sendCurrentRead();
      }
    });
    current.timer = timer;
    return sent;
  }

  saveValue(fieldId, rawValue) {
    const field = this.fields.get(fieldId);
    const device = this.targetDevice();
    const value = Number(rawValue);
    const writableTypes = [0, 1, 2, 3, 4, 5, 8, 9];
    if (!field || !device || !writableTypes.includes(field.type) || !Number.isInteger(value)) return false;
    if (field.min !== null && value < field.min) return false;
    if (field.max !== null && value > field.max) return false;
    if (field.type === 9 && (!field.values?.[value] || value < 0)) return false;
    let bytes;
    if (field.type <= 5) bytes = encodeNumeric(value, field.type);
    else if (field.type === 8) bytes = encodeFloat(value);
    else if (field.type === 9) bytes = [(value & 0xff)];
    if (!this.link.sendFrame(FRAMETYPE.PARAMETER_WRITE, device.id, this.originFor(device.id), [fieldId, ...bytes])) return false;
    const name = field.name;
    this.emit('log', { level: 'info', text: `已发送 ${name} = ${value}` });
    this.#reloadRelated(fieldId);
    return true;
  }

  command(fieldId, action) {
    const field = this.fields.get(fieldId);
    if (!field || field.type !== 13 || ![1, 4, 5, 6].includes(action)) return false;
    if (!this.#sendCommandAction(fieldId, action, true)) return false;
    if (action === 1 || action === 4) {
      // Lua keeps the popup running and issues lcsQuery when the module timeout expires.
      this.#stopCommandPoll(fieldId);
      this.#startCommandPoll(fieldId);
    } else if (action === 5) {
      this.#stopCommandPoll(fieldId);
    }
    return true;
  }

  #sendCommandAction(fieldId, action, userVisible) {
    const field = this.fields.get(fieldId);
    const device = this.targetDevice();
    if (!field || !device || field.type !== 13) return false;
    this.commandPending.add(fieldId);
    const sent = this.link.sendFrame(
      FRAMETYPE.PARAMETER_WRITE,
      device.id,
      this.originFor(device.id),
      [fieldId, action],
    );
    if (!sent) this.commandPending.delete(fieldId);
    if (sent && userVisible) {
      const actionName = { 1: '执行', 4: '确认', 5: '取消', 6: '查询' }[action] || action;
      this.emit('log', { level: 'info', text: `命令 ${field.name}: ${actionName}` });
    }
    return sent;
  }

  #manageCommand(field) {
    if (field.type !== 13) return;
    if ([1, 2, 4, 5, 6].includes(field.status)) this.#startCommandPoll(field.id);
    else if (field.status === 3 || field.status === 0) this.#stopCommandPoll(field.id);
  }

  #startCommandPoll(fieldId) {
    if (this.queryTimers.has(fieldId)) return;
    const timer = setInterval(() => {
      const field = this.fields.get(fieldId);
      if (!field) {
        this.#stopCommandPoll(fieldId);
        return;
      }
      if (this.commandPending.has(fieldId) || [1, 2, 4, 5, 6].includes(field.status)) {
        this.#sendCommandAction(fieldId, 6, false);
      } else {
        this.#stopCommandPoll(fieldId);
      }
    }, Math.max(200, (this.fields.get(fieldId)?.timeout || 20) * 10));
    this.queryTimers.set(fieldId, timer);
  }

  #stopCommandPoll(fieldId) {
    const timer = this.queryTimers.get(fieldId);
    if (timer) clearInterval(timer);
    this.queryTimers.delete(fieldId);
  }

  #reloadRelated(fieldId) {
    const field = this.fields.get(fieldId);
    if (!field) return;
    const ids = [fieldId];
    if (field.parent) {
      ids.push(field.parent);
      for (const [id, candidate] of this.fields) {
        if (id !== fieldId && candidate.parent === field.parent && candidate.loaded) {
          const t = candidate.type;
          if (t < 11 || t === 12) ids.push(id);
        }
      }
    }
    // Match the Lua script's short EEPROM-commit delay before reading back.
    this.#afterDelay(200, () => this.reloadFields(ids));
  }

  #afterDelay(ms, fn) {
    let timer;
    timer = setTimeout(() => {
      this.timers.delete(timer);
      fn();
    }, ms);
    this.timers.add(timer);
    return timer;
  }

  #clearCurrentTimer() {
    const timer = this.current?.timer;
    if (!timer) return;
    clearTimeout(timer);
    this.timers.delete(timer);
    this.current.timer = null;
  }

  #clearReadTimers() {
    for (const timer of this.timers) clearTimeout(timer);
    this.timers.clear();
    this.current = null;
    this.partials.clear();
    this.queue = [];
  }

  #clearAllTimers() {
    this.#clearReadTimers();
    for (const timer of this.queryTimers.values()) clearInterval(timer);
    this.queryTimers.clear();
    this.commandPending.clear();
  }
}
