import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import WebSocket from 'ws';

const projectDir = fileURLToPath(new URL('..', import.meta.url));

async function startServer() {
  const child = spawn(process.execPath, ['server/server.js'], {
    cwd: projectDir,
    env: { ...process.env, ELRS_UI_PORT: '0' },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let stderr = '';
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', (chunk) => { stderr += chunk; });
  child.stdout.setEncoding('utf8');
  const url = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`server start timeout: ${stderr}`)), 5000);
    child.once('exit', (code) => {
      clearTimeout(timer);
      reject(new Error(`server exited with ${code}: ${stderr}`));
    });
    child.stdout.on('data', (chunk) => {
      const match = chunk.match(/http:\/\/127\.0\.0\.1:(\d+)/);
      if (match) {
        clearTimeout(timer);
        resolve(`http://127.0.0.1:${match[1]}`);
      }
    });
  });
  return { child, url };
}

async function waitFor(predicate, ms = 5000) {
  const started = Date.now();
  while (Date.now() - started < ms) {
    const value = predicate();
    if (value) return value;
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  throw new Error(`condition timed out after ${ms}ms`);
}

test('HTTP and WebSocket demo path works end to end', async (t) => {
  const { child, url } = await startServer();
  t.after(() => child.kill('SIGTERM'));

  const page = await fetch(url);
  assert.equal(page.status, 200);
  assert.match(await page.text(), /ExpressLRS Lua 串口控制台/);

  const ports = await (await fetch(`${url}/api/ports`)).json();
  assert.equal(ports.ports[0].path, '__demo__');

  const ws = new WebSocket(url.replace('http:', 'ws:') + '/ws');
  t.after(() => ws.close());
  const messages = [];
  ws.on('message', (raw) => messages.push(JSON.parse(raw.toString())));
  await new Promise((resolve, reject) => {
    ws.once('open', resolve);
    ws.once('error', reject);
  });
  ws.send(JSON.stringify({ type: 'open', path: '__demo__' }));

  await waitFor(() => messages.some((message) => message.type === 'load-done'));
  await waitFor(() => messages.some((message) => message.type === 'telemetry' && message.telemetry.totalFrames > 0));
  const devices = messages.find((message) => message.type === 'devices')?.devices;
  const fields = messages.filter((message) => message.type === 'field');
  assert.equal(devices?.[0]?.id, 0xee);
  assert.equal(fields.length, 20);
  assert.deepEqual(fields.find((message) => message.field.id === 20)?.field.values, ['Off', '']);

  const telemetry = [...messages].reverse().find((message) => message.type === 'telemetry')?.telemetry;
  assert.ok(telemetry.totalFrames > 0);
  assert.equal(telemetry.latest.link.values.uplinkLinkQuality, 98);
  assert.equal(telemetry.latest.battery.values.voltageV, 16.8);

  const values = [1500, 1500, 1000, 1500, ...Array(12).fill(1000)];
  ws.send(JSON.stringify({ type: 'channels-set', values }));
  ws.send(JSON.stringify({ type: 'channels-enable', enabled: true }));
  await waitFor(() => messages.some((message) => message.type === 'channels' && message.channels.enabled));
  const running = [...messages].reverse().find((message) => message.type === 'channels' && message.channels.enabled)?.channels;
  assert.equal(running.rateHz, 50);
  assert.deepEqual(running.values, values);

  ws.close();
  await new Promise((resolve) => ws.once('close', resolve));
  const ws2 = new WebSocket(url.replace('http:', 'ws:') + '/ws');
  t.after(() => ws2.close());
  const snapshot = await new Promise((resolve, reject) => {
    ws2.once('message', (raw) => resolve(JSON.parse(raw.toString())));
    ws2.once('error', reject);
  });
  assert.equal(snapshot.channels.enabled, false);
  assert.match(snapshot.channels.stopReason, /页面已断开/);
});
