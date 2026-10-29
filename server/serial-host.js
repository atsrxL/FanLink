import { EventEmitter } from 'node:events';
import { SerialPort } from 'serialport';
import { buildFrame, buildStandardFrame, CrsfDecoder, ADDR } from './crsf.js';

// Order matters: 400000 is the normal CRSF handset baud. The ELRS TX UART WDT
// also cycles 115200/921600/1870000/2250000/3750000, so the host scans these
// until a valid frame is received. Rates above ~2M are usually beyond a CH340.
// Matches CRSFHandset's UART WDT cycle for ESP8266/ESP8285 targets.
export const AUTO_BAUDS = [400000, 115200, 5250000, 3750000, 1870000, 921600, 2250000];

const PROBE_WAIT_MS = 800;
const LOST_AFTER_MS = 2600;
const PING_INTERVAL_MS = 500;

async function openPort(path, baudRate) {
  const port = new SerialPort({ path, baudRate, autoOpen: false });
  await new Promise((resolve, reject) => {
    port.open((err) => (err ? reject(err) : resolve()));
  });
  return port;
}

function closePort(port) {
  return new Promise((resolve) => {
    if (!port?.isOpen) return resolve();
    port.close(() => resolve());
  });
}

export async function listSerialPorts() {
  const ports = await SerialPort.list();
  return ports
    .filter((p) => (p.path.startsWith('/dev/tty.') || p.path.startsWith('/dev/cu.')) && !p.path.includes('Bluetooth'))
    .map((p) => ({
      path: p.path.startsWith('/dev/tty.') ? p.path.replace('/dev/tty.', '/dev/cu.') : p.path,
      vendorId: p.vendorId,
      productId: p.productId,
      serialNumber: p.serialNumber,
      manufacturer: p.manufacturer,
    }));
}

// Pretends to be an EdgeTX radio on the TX module's CRSF UART: the module only
// answers Lua/parameter traffic after it receives a valid frame from its handset.
export class SerialHost extends EventEmitter {
  constructor(path, { bauds = AUTO_BAUDS } = {}) {
    super();
    this.path = path;
    this.bauds = [...bauds];
    this.port = null;
    this.decoder = new CrsfDecoder((frame) => this.#onFrame(frame));
    this.sawValid = false;
    this.lastFrameAt = 0;
    this.probeTimer = null;
    this.connectTimer = null;
    this.keepTimer = null;
    this.baudIndex = 0;
    this.attempts = 0;
    this.closedByUser = false;
  }

  async start() {
    this.closedByUser = false;
    await this.#nextBaud();
  }

  async stop() {
    this.closedByUser = true;
    this.#clearTimers();
    const port = this.port;
    this.port = null;
    await closePort(port);
    this.#state('closed');
  }

  sendFrame(type, dest, orig, payload = []) {
    const frame = buildFrame(type, dest, orig, payload);
    if (!this.port?.isOpen) return false;
    try {
      this.port.write(frame);
      this.emit('tx', { type, dest, orig, payload: [...payload] });
      return true;
    } catch {
      return false;
    }
  }

  sendStandardFrame(type, payload = []) {
    const frame = buildStandardFrame(type, payload);
    if (!this.port?.isOpen || !this.sawValid) return false;
    try {
      this.port.write(frame);
      this.emit('tx', { type, dest: null, orig: null, payload: [...payload] });
      return true;
    } catch {
      return false;
    }
  }

  ping() {
    // Broadcast device ping, exactly like elrs.lua's initial discovery.
    this.sendFrame(0x28, ADDR.BROADCAST, ADDR.RADIO);
  }

  #state(mode, extra = {}) {
    this.emit('state', {
      mode,
      path: this.path,
      baud: this.port?.isOpen ? this.bauds[this.baudIndex] : null,
      sawValid: this.sawValid,
      attempts: this.attempts,
      ...extra,
    });
  }

  #log(message) {
    this.emit('log', { level: 'info', text: message });
  }

  #clearTimers() {
    clearTimeout(this.probeTimer);
    clearInterval(this.connectTimer);
    clearInterval(this.keepTimer);
    this.probeTimer = null;
    this.connectTimer = null;
    this.keepTimer = null;
  }

  async #closeCurrent() {
    const current = this.port;
    this.port = null;
    if (current) {
      current.removeAllListeners('data');
      current.removeAllListeners('error');
      current.removeAllListeners('close');
      await closePort(current);
    }
  }

  async #nextBaud() {
    if (this.closedByUser) return;
    await this.#closeCurrent();
    this.#clearTimers();
    this.sawValid = false;
    this.decoder = new CrsfDecoder((frame) => this.#onFrame(frame));

    if (this.baudIndex >= this.bauds.length) {
      this.baudIndex = 0;
      this.attempts++;
    }
    const baud = this.bauds[this.baudIndex++];
    this.#state('connecting', { baud });
    if (this.attempts === 0 || (this.attempts < 3 && this.baudIndex === 1)) {
      this.#log(`尝试 ${this.path} @ ${baud} baud ...`);
    }

    try {
      const port = await openPort(this.path, baud);
      if (this.closedByUser) {
        await closePort(port);
        return;
      }
      this.port = port;
      port.on('data', (data) => {
        try {
          this.decoder.push(data);
        } catch {}
      });
      port.on('error', (err) => {
        this.#log(`串口错误: ${err.message}`);
      });
      port.on('close', () => {
        if (!this.closedByUser && this.sawValid) {
          this.#log('串口被关闭，重新连接');
          this.#nextBaud();
        }
      });

      setTimeout(() => this.ping(), 60);
      this.connectTimer = setInterval(() => this.ping(), 100);
      this.probeTimer = setTimeout(() => {
        if (!this.sawValid && !this.closedByUser) {
          this.#nextBaud();
        }
      }, PROBE_WAIT_MS);
    } catch (err) {
      if (!this.closedByUser) {
        if (this.baudIndex === 1) this.#log(`${this.path} 打开失败: ${err.message}`);
        setTimeout(() => this.#nextBaud(), 120);
      }
    }
  }

  #onFrame(frame) {
    if (!this.sawValid) {
      this.sawValid = true;
      this.#clearTimers();
      this.#log(`CRSF 链路已建立 @ ${this.bauds[this.baudIndex - 1]} baud`);
      this.#state('online', { baud: this.bauds[this.baudIndex - 1] });
      this.keepTimer = setInterval(() => {
        if (!this.port?.isOpen) return;
        if (Date.now() - this.lastFrameAt > LOST_AFTER_MS) {
          this.#log('链路超时，重新扫描波特率');
          this.#nextBaud();
          return;
        }
        this.ping();
      }, PING_INTERVAL_MS);
    }
    this.lastFrameAt = Date.now();
    this.emit('frame', frame);
  }
}
