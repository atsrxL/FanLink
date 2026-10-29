import { SerialHost } from '../server/serial-host.js';

const path = process.argv[2];
if (!path) {
  console.error('usage: node test/probe-serial.mjs /dev/cu.xxx');
  process.exit(2);
}
const host = new SerialHost(path);
host.on('state', (s) => console.log('[state]', JSON.stringify(s)));
host.on('log', ({ text }) => console.log('[log]', text));
host.on('frame', (f) => {
  console.log('[frame]', `type=0x${f.type.toString(16)}`, `dest=0x${f.dest.toString(16)}`, `orig=0x${f.orig.toString(16)}`, 'payload=', f.payload.toString('hex'));
});
await host.start();
setTimeout(async () => {
  await host.stop();
  process.exit(0);
}, 10000);
