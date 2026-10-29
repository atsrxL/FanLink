import WebSocket from 'ws';
import assert from 'node:assert/strict';

const ws = new WebSocket(process.env.ELRS_UI_WS || 'ws://127.0.0.1:8598/ws');
const got = [];
const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

ws.on('message', (raw) => {
  const msg = JSON.parse(raw.toString());
  got.push(msg);
});

await new Promise((resolve) => ws.on('open', resolve));
ws.send(JSON.stringify({ type: 'open', path: '__demo__' }));
await wait(1800);
const fields = got.filter((m) => m.type === 'field');
const devicesMsg = got.find((m) => m.type === 'devices');
console.log('devices:', JSON.stringify(devicesMsg?.devices));
console.log('fields received:', fields.length);
console.log('last field:', JSON.stringify(fields.at(-1)));
assert.equal(devicesMsg?.devices?.[0]?.id, 0xee, 'demo TX module was not discovered');
assert.equal(fields.length, 20, 'not all demo fields were loaded');
const telemetry = got.filter((m) => m.type === 'telemetry').at(-1)?.telemetry;
assert.ok(telemetry?.latest?.link, 'demo link telemetry was not received');
assert.ok(telemetry?.latest?.battery, 'demo battery telemetry was not received');
const channelState = got.find((m) => m.type === 'snapshot')?.channels;
assert.equal(channelState?.enabled, false, 'RC output must default to disabled');

const saveTarget = fields.find((m) => m.field.type === 9 && m.field.values?.includes('Wide'));
if (saveTarget) {
  ws.send(JSON.stringify({ type: 'save', fieldId: saveTarget.field.id, value: 0 }));
  await wait(500);
  const updated = got.filter((m) => m.type === 'field' && m.field.id === saveTarget.field.id).at(-1);
  console.log('after save:', JSON.stringify(updated?.field));
  assert.equal(updated?.field?.value, 0, 'saved selection was not read back');
}
assert.ok(saveTarget, 'Switch Mode selection was not found');

const cmd = got.find((m) => m.type === 'field' && m.field.type === 13 && m.field.name === 'Enable WiFi');
if (cmd) {
  ws.send(JSON.stringify({ type: 'command', fieldId: cmd.field.id, action: 1 }));
  await wait(350);
  const ask = got.filter((m) => m.type === 'field' && m.field.id === cmd.field.id).at(-1);
  console.log('after run:', JSON.stringify(ask?.field));
  assert.equal(ask?.field?.status, 3, 'command did not request confirmation');
  ws.send(JSON.stringify({ type: 'command', fieldId: cmd.field.id, action: 4 }));
  await wait(1800);
  const done = got.filter((m) => m.type === 'field' && m.field.id === cmd.field.id).at(-1);
  console.log('after confirm:', JSON.stringify(done?.field));
  assert.equal(done?.field?.status, 0, 'command did not finish');
}
assert.ok(cmd, 'Enable WiFi command was not found');

ws.close();
process.exit(0);
