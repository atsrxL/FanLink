import { SerialPort } from 'serialport';
import { buildFrame } from '../server/crsf.js';

const path = process.argv[2] || '/dev/cu.usbmodem5C4C0540041';
const mode = process.argv[3] || 'noflow';
const bauds = [400000, 115200, 921600, 460800, 500000, 1000000, 2000000, 3000000];
const ping = buildFrame(0x28, 0, 0xea);

for (const baud of bauds) {
  await new Promise((resolve) => {
    const port = new SerialPort({
      path,
      baudRate: baud,
      autoOpen: false,
      dtr: mode !== 'flow',
      rts: mode !== 'flow',
    });
    const rx = [];
    port.open((err) => {
      if (err) {
        console.log(`baud ${baud}: OPEN ERR ${err.message}`);
        resolve();
        return;
      }
      port.on('data', (d) => rx.push(...d));
      const send = setInterval(() => {
        try { port.write(ping); } catch {}
      }, 40);
      setTimeout(() => {
        clearInterval(send);
        port.close(() => {
          if (rx.length) console.log(`baud ${baud}: RX ${rx.length}B ${Buffer.from(rx).toString('hex').slice(0, 160)}`);
          else console.log(`baud ${baud}: no RX`);
          resolve();
        });
      }, 1500);
    });
  });
}
process.exit(0);
