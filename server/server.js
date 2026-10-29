import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import { SerialHost, listSerialPorts } from './serial-host.js';
import { SimHost } from './sim-host.js';
import { ElrsProtocol } from './elrs-protocol.js';
import { TelemetryStore } from './telemetry.js';
import { RcChannelController, SAFE_CHANNEL_VALUES } from './rc-channels.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const publicDir = path.join(__dirname, '..', 'public');
const PORT = Number(process.env.ELRS_UI_PORT || 8598);
const HOST = '127.0.0.1';

const logs = [];
function pushLog(level, text) {
  const entry = { level, text, at: new Date().toISOString().slice(11, 19) };
  logs.push(entry);
  if (logs.length > 300) logs.shift();
  broadcast({ type: 'log', ...entry });
}

let session = null;
let lastState = { mode: 'closed' };
let channelOwner = null;

function idleChannelSnapshot(reason = null) {
  return {
    enabled: false,
    values: [...SAFE_CHANNEL_VALUES],
    rateHz: 50,
    sentFrames: 0,
    lastSentAt: null,
    stopReason: reason,
    min: 1000,
    max: 2000,
  };
}

function emptyTelemetrySnapshot() {
  return { latest: {}, recent: [], totalFrames: 0, lastFrameAt: null, rateHz: 0 };
}

function broadcast(obj) {
  for (const ws of wss.clients) {
    if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
  }
}

function relaySessionEvents() {
  if (!session) return;
  const active = session;
  const { link, protocol, telemetry, channels } = active;
  const current = () => session === active;
  link.on('state', (state) => {
    if (!current()) return;
    lastState = state;
    if (state.mode !== 'online' && channels.enabled) {
      channelOwner = null;
      channels.disable('串口不在线，已自动停发');
      pushLog('warn', 'RC 通道输出因串口离线已自动停止');
    }
    broadcast({ type: 'state', ...state });
  });
  link.on('frame', (frame) => telemetry.accept(frame));
  link.on('log', ({ level, text }) => current() && pushLog(level, text));
  protocol.on('log', ({ level, text }) => current() && pushLog(level, text));
  protocol.on('device-list', () => {
    if (!current()) return;
    broadcast({
      type: 'devices',
      devices: protocol.deviceList(),
      target: protocol.targetId,
    });
  });
  protocol.on('field', (field) => current() && broadcast({ type: 'field', field }));
  protocol.on('load-reset', () => current() && broadcast({ type: 'load-reset' }));
  protocol.on('load-start', ({ total }) => current() && broadcast({ type: 'load-start', total }));
  protocol.on('load-done', () => current() && broadcast({ type: 'load-done' }));
  protocol.on('progress', (progress) => current() && broadcast({ type: 'progress', ...progress }));
  protocol.on('linkstat', (linkstat) => current() && broadcast({ type: 'linkstat', linkstat }));
  telemetry.on('update', () => {
    if (!current() || active.telemetryTimer) return;
    active.telemetryTimer = setTimeout(() => {
      active.telemetryTimer = null;
      if (current()) broadcast({ type: 'telemetry', telemetry: telemetry.snapshot() });
    }, 80);
  });
  channels.on('change', (snapshot) => {
    if (current()) broadcast({ type: 'channels', channels: snapshot });
  });
}

async function openSession(kind, portPath) {
  await closeSession();
  const link = kind === 'demo' ? new SimHost() : new SerialHost(portPath);
  const protocol = new ElrsProtocol(link);
  const telemetry = new TelemetryStore();
  const channels = new RcChannelController(link);
  session = { link, protocol, telemetry, channels, telemetryTimer: null, kind };
  relaySessionEvents();
  lastState = { mode: 'connecting', path: kind === 'demo' ? '模拟设备' : portPath };
  broadcast({ type: 'state', ...lastState });
  pushLog('info', kind === 'demo' ? '启动本机模拟模式…' : `连接 ${portPath}，正在扫描 CRSF 波特率…`);
  await link.start();
  if (kind === 'demo') {
    setTimeout(() => link.ping(), 300);
  }
}

async function closeSession() {
  if (!session) return;
  const old = session;
  channelOwner = null;
  old.channels.disable('会话已关闭');
  clearTimeout(old.telemetryTimer);
  session = null;
  try {
    await old.link.stop();
  } catch {}
  lastState = { mode: 'closed' };
  broadcast({ type: 'state', ...lastState });
  broadcast({ type: 'channels', channels: idleChannelSnapshot('会话已关闭') });
  broadcast({ type: 'telemetry', telemetry: emptyTelemetrySnapshot() });
  pushLog('info', '已断开');
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  if (url.pathname === '/api/ports' && req.method === 'GET') {
    listSerialPorts()
      .then((ports) => {
        res.writeHead(200, { 'content-type': 'application/json' });
        res.end(JSON.stringify({ ports: [{ path: '__demo__', label: '演示模拟器（无串口）' }, ...ports.map((p) => ({ ...p, label: p.path }))] }));
      })
      .catch((err) => {
        res.writeHead(500, { 'content-type': 'application/json' });
        res.end(JSON.stringify({ error: err.message }));
      });
    return;
  }

  let file = 'index.html';
  if (url.pathname.startsWith('/app')) file = url.pathname.slice(1);
  if (url.pathname !== '/' && url.pathname.startsWith('/public/')) file = url.pathname.slice(1);
  const target = path.normalize(path.join(publicDir, file));
  if (!target.startsWith(publicDir)) {
    res.writeHead(403);
    res.end();
    return;
  }
  fs.readFile(target, (err, data) => {
    if (err) {
      res.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' });
      res.end('Not found');
      return;
    }
    const ext = path.extname(target);
    const mime = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8' }[ext] || 'application/octet-stream';
    res.writeHead(200, { 'content-type': mime, 'cache-control': 'no-store' });
    res.end(data);
  });
});

const wss = new WebSocketServer({ server, path: '/ws' });
wss.on('connection', (ws) => {
  const snapshot = {
    type: 'snapshot',
    state: lastState,
    logs: logs.slice(-200),
    devices: session?.protocol.deviceList() || [],
    target: session?.protocol.targetId || 0xee,
    fields: session ? [...session.protocol.fields.values()] : [],
    linkstat: session?.protocol.linkStat || null,
    loading: session?.protocol.loading || false,
    telemetry: session?.telemetry.snapshot() || emptyTelemetrySnapshot(),
    channels: session?.channels.snapshot() || idleChannelSnapshot(),
  };
  ws.send(JSON.stringify(snapshot));

  ws.on('message', (raw) => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      return;
    }
    const reply = (obj) => ws.send(JSON.stringify(obj));
    switch (msg.type) {
      case 'open': {
        const kind = msg.path === '__demo__' ? 'demo' : 'serial';
        openSession(kind, msg.path).catch((err) => reply({ type: 'error', text: err.message }));
        break;
      }
      case 'close':
        closeSession().catch(() => {});
        break;
      case 'selectTarget':
        if (session?.protocol.selectTarget(msg.id)) reply({ type: 'ok' });
        break;
      case 'save':
        if (session?.protocol.saveValue(msg.fieldId, msg.value)) reply({ type: 'ok' });
        else reply({ type: 'error', text: '参数值无效或当前未连接' });
        break;
      case 'command':
        if (session?.protocol.command(msg.fieldId, msg.action)) reply({ type: 'ok' });
        else reply({ type: 'error', text: '命令无效或当前未连接' });
        break;
      case 'reload':
        if (session?.protocol.reloadAll()) reply({ type: 'ok' });
        else reply({ type: 'error', text: '尚未发现可读取的设备' });
        break;
      case 'channels-set':
        if (!session) {
          reply({ type: 'error', text: '当前没有活动串口会话' });
        } else if (channelOwner && channelOwner !== ws) {
          reply({ type: 'error', text: '通道输出正由另一个页面控制' });
        } else {
          try {
            session.channels.setValues(msg.values);
            reply({ type: 'ok' });
          } catch (err) {
            reply({ type: 'error', text: err.message });
          }
        }
        break;
      case 'channels-reset':
        if (!session) {
          reply({ type: 'error', text: '当前没有活动串口会话' });
        } else if (channelOwner && channelOwner !== ws) {
          reply({ type: 'error', text: '通道输出正由另一个页面控制' });
        } else {
          session.channels.reset(msg.profile === 'center' ? 'center' : 'safe');
          reply({ type: 'ok' });
        }
        break;
      case 'channels-enable':
        if (!msg.enabled) {
          channelOwner = null;
          session?.channels.disable('页面已关闭输出');
          if (session) pushLog('info', 'RC 通道输出已停止');
          reply({ type: 'ok' });
        } else if (!session || lastState.mode !== 'online') {
          reply({ type: 'error', text: '串口尚未在线，不能启用通道输出' });
        } else if (channelOwner && channelOwner !== ws) {
          reply({ type: 'error', text: '通道输出正由另一个页面控制' });
        } else {
          channelOwner = ws;
          if (session.channels.enable()) {
            pushLog('warn', `RC 通道输出已启用 (${session.channels.rateHz} Hz)`);
            reply({ type: 'ok' });
          } else {
            channelOwner = null;
            reply({ type: 'error', text: '无法启动通道输出' });
          }
        }
        break;
    }
  });

  ws.on('close', () => {
    if (channelOwner !== ws) return;
    channelOwner = null;
    session?.channels.disable('控制页面已断开，已自动停发');
    pushLog('warn', 'RC 通道输出因控制页面断开已自动停止');
  });
});

server.listen(PORT, HOST, () => {
  const address = server.address();
  const listeningPort = typeof address === 'object' && address ? address.port : PORT;
  console.log(`ELRS Lua 本地界面: http://${HOST}:${listeningPort}`);
  console.log('按 Ctrl+C 停止。');
});
