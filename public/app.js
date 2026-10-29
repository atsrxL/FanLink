import { displayedValue, valueScale, wireValue } from './app-field-values.js';

const $ = (id) => document.getElementById(id);
const SAFE_CHANNEL_VALUES = [1500, 1500, 1000, 1500, ...Array(12).fill(1000)];
const state = {
  ports: [],
  devices: [],
  target: 0xee,
  fields: new Map(),
  collapsed: new Set(),
  drafts: new Map(),
  total: 0,
  loading: false,
  connMode: 'closed',
  baud: null,
  path: null,
  link: null,
  telemetry: { latest: {}, recent: [], totalFrames: 0, lastFrameAt: null, rateHz: 0 },
  channels: {
    enabled: false,
    values: [...SAFE_CHANNEL_VALUES],
    rateHz: 50,
    sentFrames: 0,
    lastSentAt: null,
    stopReason: null,
    min: 1000,
    max: 2000,
  },
};

const els = {
  port: $('portSelect'),
  connect: $('connectBtn'),
  refresh: $('refreshPorts'),
  reload: $('reloadBtn'),
  connDot: $('connDot'),
  connText: $('connText'),
  devices: $('deviceList'),
  linkPills: $('linkPills'),
  linkBox: $('linkBox'),
  deviceTitle: $('deviceTitle'),
  tree: $('fieldTree'),
  fieldEmpty: $('fieldEmpty'),
  logs: $('logList'),
  progressRow: document.querySelector('.progress-row'),
  progress: $('progressBar'),
  progressText: $('progressText'),
  telemetryStatus: $('telemetryStatus'),
  telemetryRate: $('telemetryRate'),
  telemetryCards: $('telemetryCards'),
  telemetryFrames: $('telemetryFrames'),
  telemetryCount: $('telemetryCount'),
  channelsPanel: $('channelsPanel'),
  channelGrid: $('channelGrid'),
  channelStatus: $('channelStatus'),
  channelNotice: $('channelNotice'),
  channelOutput: $('channelOutput'),
  safeChannels: $('safeChannels'),
  centerChannels: $('centerChannels'),
};

let ws = null;
let wsRetry = null;
let channelSendTimer = null;

function connectWs() {
  clearTimeout(wsRetry);
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onopen = () => {};
  ws.onclose = () => {
    if (state.channels.enabled) {
      state.channels.enabled = false;
      state.channels.stopReason = '页面连接已断开，服务端会自动停发';
      renderChannels();
    }
    wsRetry = setTimeout(connectWs, 800);
  };
  ws.onmessage = (event) => {
    let msg;
    try { msg = JSON.parse(event.data); } catch { return; }
    handle(msg);
  };
}

function send(obj) {
  if (ws?.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

function updateReloadState() {
  els.reload.disabled = state.connMode !== 'online' || !state.devices.length;
}

function normalizeSessionPath(path) {
  return path === 'simulator' || path === '模拟设备' ? '__demo__' : path;
}

function syncSelectedPort() {
  const path = normalizeSessionPath(state.path);
  if (path && state.ports.some((port) => port.path === path)) els.port.value = path;
}

function logLine({ level = 'info', text, at } = {}) {
  const row = document.createElement('div');
  row.className = `log-line ${level}`;
  const time = document.createElement('time');
  time.textContent = at || new Date().toTimeString().slice(0, 8);
  const body = document.createElement('span');
  body.textContent = text;
  row.append(time, body);
  els.logs.append(row);
  while (els.logs.children.length > 250) els.logs.firstChild.remove();
  els.logs.scrollTop = els.logs.scrollHeight;
}

function conn(mode, extra = {}) {
  state.connMode = mode;
  state.baud = extra.baud ?? state.baud;
  state.path = extra.path ?? state.path;
  syncSelectedPort();
  els.connDot.className = `dot ${mode === 'online' ? 'on' : mode === 'connecting' ? 'connecting' : ''}`;
  if (mode === 'online') els.connText.textContent = `已连接 @ ${state.baud} baud`;
  else if (mode === 'connecting') els.connText.textContent = `扫描波特率… ${extra.path || ''}`;
  else els.connText.textContent = '未连接';
  els.connect.textContent = mode === 'closed' ? '连接' : '断开';
  els.connect.classList.toggle('primary', mode === 'closed');
  els.connect.classList.toggle('danger', mode !== 'closed');
  updateReloadState();
  renderChannels();
  if (mode === 'closed') {
    state.devices = [];
    state.fields.clear();
    state.target = 0xee;
    state.link = null;
    els.linkPills.hidden = true;
    els.linkBox.hidden = true;
    els.linkPills.replaceChildren();
    els.linkBox.replaceChildren();
    state.telemetry = { latest: {}, recent: [], totalFrames: 0, lastFrameAt: null, rateHz: 0 };
    state.channels.enabled = false;
    renderDevices();
    renderTree();
    renderTelemetry();
    renderChannels();
  }
}

function handle(msg) {
  switch (msg.type) {
    case 'snapshot':
      els.logs.replaceChildren();
      for (const line of msg.logs || []) logLine(line);
      if (msg.state?.mode) conn(msg.state.mode, msg.state);
      applyDevices(msg.devices || [], msg.target);
      state.fields = new Map((msg.fields || []).map((f) => [f.id, f]));
      if (msg.linkstat) {
        state.link = msg.linkstat;
        renderLink(msg.linkstat);
      }
      state.loading = !!msg.loading;
      if (msg.telemetry) state.telemetry = msg.telemetry;
      if (msg.channels) state.channels = msg.channels;
      renderTree();
      renderTelemetry();
      renderChannels();
      break;
    case 'state':
      conn(msg.mode, msg);
      break;
    case 'devices':
      applyDevices(msg.devices || [], msg.target);
      break;
    case 'field': {
      const old = state.fields.get(msg.field.id);
      state.fields.set(msg.field.id, msg.field);
      if (state.drafts.has(msg.field.id)) state.drafts.delete(msg.field.id);
      if (old?.type === 13 && old.status !== msg.field.status) logLine({ level: 'info', text: `命令 ${msg.field.name}: ${cmdStatus(msg.field)}` });
      renderTree();
      break;
    }
    case 'load-reset':
      state.fields.clear();
      state.drafts.clear();
      renderTree();
      break;
    case 'load-start':
      state.loading = true;
      state.total = msg.total;
      els.progressRow.hidden = false;
      els.progress.max = msg.total;
      els.progress.value = 0;
      els.progressText.textContent = '0 / ' + msg.total;
      renderTree();
      break;
    case 'progress':
      state.total = msg.total;
      els.progress.max = msg.total;
      els.progress.value = msg.done;
      els.progressText.textContent = `${msg.done} / ${msg.total}`;
      break;
    case 'load-done':
      state.loading = false;
      els.progressRow.hidden = true;
      break;
    case 'linkstat':
      state.link = msg.linkstat;
      renderLink(msg.linkstat);
      break;
    case 'telemetry':
      state.telemetry = msg.telemetry || state.telemetry;
      renderTelemetry();
      break;
    case 'channels':
      state.channels = msg.channels || state.channels;
      renderChannels();
      break;
    case 'log':
      logLine(msg);
      break;
    case 'error':
      logLine({ level: 'error', text: msg.text });
      break;
  }
}

function applyDevices(devices, target) {
  state.devices = devices;
  state.target = target ?? 0xee;
  updateReloadState();
  renderDevices();
  const device = devices.find((d) => d.id === state.target);
  els.deviceTitle.textContent = device ? `${device.name} (0x${device.id.toString(16).toUpperCase()})` : state.connMode === 'online' ? '等待设备信息…' : '等待设备';
}

function renderDevices() {
  if (!state.devices.length) {
    els.devices.innerHTML = '<p class="empty">未发现设备</p>';
    return;
  }
  els.devices.replaceChildren();
  for (const device of state.devices) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = `device ${device.id === state.target ? 'active' : ''}`;
    btn.dataset.id = device.id;
    const title = document.createElement('b');
    title.textContent = device.name || `0x${device.id.toString(16).toUpperCase()}`;
    const sub = document.createElement('small');
    sub.textContent = `0x${device.id.toString(16).toUpperCase()} · ${device.fldcnt} 参数`;
    const tag = document.createElement('span');
    tag.className = 'tag';
    tag.textContent = device.isElrs ? 'ELRS' : 'CRSF';
    btn.append(title, sub, tag);
    els.devices.append(btn);
  }
  els.devices.querySelectorAll('.device').forEach((btn) => {
    btn.onclick = () => send({ type: 'selectTarget', id: Number(btn.dataset.id) });
  });
}

function renderLink(link) {
  if (!link) return;
  const waitingForChannels = state.connMode === 'online' && !link.connectedBit && !state.channels.enabled;
  els.linkPills.hidden = false;
  els.linkPills.replaceChildren();
  const pills = [
    link.connectedBit ? ['RF 链路已连接', 'ok'] : waitingForChannels ? ['RF 等待通道输出', 'warn'] : ['RF 链路未连接', 'bad'],
    link.armed ? ['已解锁 ARM', 'warn'] : null,
    link.modelMismatch ? ['Model Match 不匹配', 'warn'] : null,
    [`坏包 ${link.bad} / 好包 ${link.good}`, link.bad > link.good * 0.1 ? 'warn' : 'ok'],
  ].filter(Boolean);
  for (const [text, cls] of pills) {
    const pill = document.createElement('span');
    pill.className = `pill ${cls}`;
    pill.textContent = text;
    els.linkPills.append(pill);
  }
  els.linkBox.hidden = false;
  els.linkBox.replaceChildren();
  const lines = [
    ['坏/好包', `${link.bad}/${link.good}`],
    ['标志位', `0x${link.flags.toString(16)}`],
  ];
  if (waitingForChannels) lines.push(['当前状态', 'RC 输出已关闭；ELRS 不会继续发送陈旧通道值']);
  if (link.msg) lines.push(['提示', link.msg]);
  for (const [k, v] of lines) {
    const row = document.createElement('div');
    row.textContent = `${k}: ${v}`;
    els.linkBox.append(row);
  }
}

function cmdStatus(f) {
  const map = { 0: '空闲/完成', 1: '已点击', 2: '执行中', 3: '等待确认', 4: '已确认', 5: '取消中', 6: '查询中' };
  return `${map[f.status] || f.status}${f.info ? ' · ' + f.info : ''}`;
}

function displayValue(f) {
  if (f.type <= 5) return `${f.value}${f.unit ? ' ' + f.unit : ''}`;
  if (f.type === 8) {
    const digits = f.prec && f.prec > 1 ? Math.log10(f.prec) : 0;
    return `${(f.value / (f.prec || 1)).toFixed(digits)}${f.unit ? ' ' + f.unit : ''}`;
  }
  if (f.type === 9) return `${f.values?.[f.value] ?? f.value}${f.unit ? ' ' + f.unit : ''}`;
  if (f.type === 10 || f.type === 12) return f.value || '';
  return '';
}

function actionBtn(id, action, label, cls = '') {
  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = `mini-act ${cls}`;
  btn.dataset.action = String(action);
  btn.dataset.field = String(id);
  btn.textContent = label;
  return btn;
}

function rowFor(f, depth) {
  const row = document.createElement('div');
  row.className = 'field-row';
  row.dataset.id = f.id;
  const nameWrap = document.createElement('span');
  nameWrap.className = 'fname';
  nameWrap.style.paddingLeft = `${depth * 20}px`;
  const badge = document.createElement('span');
  badge.className = 'badge';
  const typeNames = { 0: 'U8', 1: 'I8', 2: 'U16', 3: 'I16', 4: 'U32', 5: 'I32', 8: 'FLOAT', 9: 'SEL', 10: 'STR', 12: 'INFO', 13: 'CMD' };
  badge.textContent = typeNames[f.type] || f.type;
  const name = document.createElement('span');
  name.textContent = f.name || `参数 ${f.id}`;
  nameWrap.append(badge, name);
  if (f.type === 11) {
    row.classList.add('folder');
    const collapsed = state.collapsed.has(f.id);
    const arrow = document.createElement('span');
    arrow.textContent = collapsed ? '▸' : '▾';
    nameWrap.prepend(arrow);
    row.append(nameWrap);
    row.dataset.folder = '1';
    row.setAttribute('aria-expanded', String(!collapsed));
    return row;
  }
  const value = document.createElement('span');
  value.className = 'fvalue';
  row.append(nameWrap, value);
  if (f.type === 13) {
    row.classList.add('command');
    const label = document.createElement('span');
    label.className = 'cmd-info';
    if (f.status === 3) {
      value.append(label, actionBtn(f.id, 4, '确认', 'save-btn'), actionBtn(f.id, 5, '取消', 'cancel-btn'));
      label.textContent = f.info || '等待确认';
    } else if (f.status === 2 || f.status === 4 || f.status === 6) {
      const spin = document.createElement('span');
      spin.className = 'cmd-spin';
      spin.textContent = '◌';
      value.append(spin, label, actionBtn(f.id, 5, '取消', 'cancel-btn'));
      label.textContent = f.info || '执行中…';
    } else if (f.status === 5) {
      label.textContent = '取消中…';
      value.append(label);
    } else {
      value.append(actionBtn(f.id, 1, '执行', 'primary'));
      if (f.info) {
        label.textContent = f.info;
        value.append(label);
      }
    }
    return row;
  }
  const text = document.createElement('span');
  text.className = 'row-num';
  text.textContent = displayValue(f);
  value.append(text);
  if (f.type <= 5 || f.type === 8) {
    row.classList.add('numeric');
    const draft = state.drafts.get(f.id);
    if (draft) {
      const input = document.createElement('input');
      input.type = 'number';
      const scale = valueScale(f);
      input.step = String((f.step || 1) / scale);
      input.min = f.min === null ? '' : String(f.min / scale);
      input.max = f.max === null ? '' : String(f.max / scale);
      input.value = String(draft.value);
      input.dataset.field = f.id;
      const actions = document.createElement('span');
      actions.className = 'edit-actions';
      actions.append(actionBtn(f.id, 'save-draft', '保存', 'save-btn'), actionBtn(f.id, 'cancel-draft', '取消', 'cancel-btn'));
      value.replaceChildren(input, actions);
    } else {
      text.title = `范围 ${f.min ?? '-'}..${f.max ?? '-'}${f.step ? '，步进 ' + f.step : ''}`;
      value.append(actionBtn(f.id, 'edit', '编辑', 'ghost'));
    }
  } else if (f.type === 9 && f.values) {
    const sel = document.createElement('select');
    sel.className = 'field-select';
    sel.dataset.field = f.id;
    f.values.forEach((opt, i) => {
      const option = document.createElement('option');
      option.value = String(i);
      option.textContent = opt || '(空)';
      option.selected = i === f.value;
      option.disabled = opt === '';
      sel.append(option);
    });
    value.replaceChildren(sel);
    if (f.unit) {
      const unit = document.createElement('span');
      unit.className = 'unit';
      unit.textContent = f.unit;
      value.append(unit);
    }
  } else if (f.type === 10 || f.type === 12) {
    row.classList.add(f.type === 12 ? 'info' : 'string');
    text.style.textAlign = 'left';
  }
  return row;
}

function sortedFields() {
  return [...state.fields.values()].sort((a, b) => a.id - b.id);
}

function childrenOf(parent) {
  return sortedFields().filter((f) => (f.parent ?? 0) === (parent ?? 0));
}

function renderTree() {
  els.tree.replaceChildren();
  const all = sortedFields();
  if (!all.length) {
    els.fieldEmpty.hidden = false;
    els.fieldEmpty.textContent = state.connMode === 'online' ? '参数读取中…' : '连接并发现 ELRS 设备后，参数会显示在这里。';
    els.tree.append(els.fieldEmpty);
    return;
  }
  els.fieldEmpty.hidden = true;
  const walk = (parent, depth) => {
    for (const f of childrenOf(parent)) {
      if (f.hidden) continue;
      if (f.type === 11) {
        els.tree.append(rowFor(f, depth));
        if (!state.collapsed.has(f.id)) walk(f.id, depth + 1);
      } else {
        els.tree.append(rowFor(f, depth));
      }
    }
  };
  walk(null, 0);
}

function clockTime(at) {
  if (!at) return '—';
  return new Date(at).toLocaleTimeString('zh-CN', { hour12: false });
}

function telemetryDetails(item) {
  const v = item.values || {};
  if (item.key === 'link') {
    return [
      ['上行 RSSI', `${v.uplinkRssi1Dbm} / ${v.uplinkRssi2Dbm} dBm`],
      ['上行 LQ / SNR', `${v.uplinkLinkQuality}% / ${v.uplinkSnrDb} dB`],
      ['下行 RSSI', `${v.downlinkRssiDbm} dBm`],
      ['下行 LQ / SNR', `${v.downlinkLinkQuality}% / ${v.downlinkSnrDb} dB`],
      ['发射功率', v.uplinkTxPowerMw === null ? `代码 ${v.uplinkTxPowerCode}` : `${v.uplinkTxPowerMw} mW`],
      ['天线 / RF 模式', `${v.activeAntenna} / ${v.rfMode}`],
    ];
  }
  if (item.key === 'battery') {
    return [
      ['电压', `${v.voltageV?.toFixed(1)} V`],
      ['电流', `${v.currentA?.toFixed(1)} A`],
      ['已用容量', `${v.capacityMah} mAh`],
      ['剩余', `${v.remainingPercent}%`],
    ];
  }
  if (item.key === 'gps') {
    return [
      ['坐标', `${v.latitude?.toFixed(7)}, ${v.longitude?.toFixed(7)}`],
      ['地速 / 航向', `${v.groundSpeedKmh?.toFixed(1)} km/h / ${v.headingDeg?.toFixed(2)}°`],
      ['高度 / 卫星', `${v.altitudeM} m / ${v.satellites}`],
    ];
  }
  if (item.key === 'attitude') {
    return [
      ['Pitch', `${v.pitchDeg?.toFixed(1)}°`],
      ['Roll', `${v.rollDeg?.toFixed(1)}°`],
      ['Yaw', `${v.yawDeg?.toFixed(1)}°`],
    ];
  }
  if (item.key === 'baro') {
    return [
      ['高度', `${v.altitudeM} m`],
      ...(v.verticalSpeedMps === undefined ? [] : [['升降率', `${v.verticalSpeedMps.toFixed(2)} m/s`]]),
    ];
  }
  if (item.key === 'barometer') return [['气压', `${v.pressurePa} Pa`], ['温度', `${v.temperatureC?.toFixed(2)} °C`]];
  if (item.key === 'vario') return [['垂直速度', `${v.verticalSpeedMps?.toFixed(2)} m/s`]];
  if (item.key === 'airspeed') return [['空速', `${v.airspeedKmh?.toFixed(1)} km/h`]];
  if (item.key === 'gpsTime') return [['UTC', v.utc]];
  if (item.key === 'flightMode') return [['模式', v.mode]];
  return [['数据', item.summary || '—']];
}

function renderTelemetry() {
  const telemetry = state.telemetry || {};
  const total = telemetry.totalFrames || 0;
  const rate = Number(telemetry.rateHz || 0);
  els.telemetryRate.textContent = `${rate.toFixed(1)} Hz`;
  els.telemetryRate.classList.toggle('live', rate > 0);
  els.telemetryCount.textContent = String(total);
  if (!total) {
    els.telemetryStatus.textContent = state.connMode === 'online'
      ? '串口在线，等待接收机 / 飞控回传'
      : '等待遥测帧';
  } else {
    els.telemetryStatus.textContent = `已接收 ${total} 帧 · 最近 ${clockTime(telemetry.lastFrameAt)}`;
  }

  const order = ['link', 'battery', 'gps', 'flightMode', 'attitude', 'vario', 'baro', 'airspeed', 'gpsTime', 'barometer', 'magnetometer'];
  const latest = Object.values(telemetry.latest || {}).filter((item) => item.known);
  latest.sort((a, b) => {
    const ai = order.indexOf(a.key);
    const bi = order.indexOf(b.key);
    return (ai < 0 ? 999 : ai) - (bi < 0 ? 999 : bi) || a.name.localeCompare(b.name);
  });
  els.telemetryCards.replaceChildren();
  if (!latest.length) {
    const empty = document.createElement('p');
    empty.className = 'empty';
    empty.textContent = total ? '收到尚未解析的 CRSF 帧，可在下方查看原始数据。' : '接收机与飞控回传的数据会显示在这里。';
    els.telemetryCards.append(empty);
  } else {
    for (const item of latest) {
      const card = document.createElement('article');
      card.className = 'telemetry-card';
      const title = document.createElement('h3');
      title.textContent = item.name;
      const details = document.createElement('dl');
      for (const [label, value] of telemetryDetails(item)) {
        const term = document.createElement('dt');
        term.textContent = label;
        const description = document.createElement('dd');
        description.textContent = value;
        details.append(term, description);
      }
      const time = document.createElement('time');
      time.textContent = clockTime(item.at);
      card.append(title, details, time);
      els.telemetryCards.append(card);
    }
  }

  els.telemetryFrames.replaceChildren();
  for (const item of (telemetry.recent || []).slice(0, 16)) {
    const row = document.createElement('div');
    row.className = 'telemetry-frame';
    const time = document.createElement('time');
    time.textContent = clockTime(item.at);
    const name = document.createElement('b');
    name.textContent = `0x${Number(item.type).toString(16).padStart(2, '0').toUpperCase()} ${item.name}`;
    const raw = document.createElement('small');
    raw.textContent = `${item.summary || ''}${item.hex ? ` · ${item.hex}` : ''}`;
    row.append(time, name, raw);
    els.telemetryFrames.append(row);
  }
}

function channelPercent(value) {
  const percent = Math.round((value - 1500) / 5);
  return `${percent > 0 ? '+' : ''}${percent}%`;
}

function initChannelGrid() {
  els.channelGrid.replaceChildren();
  for (let index = 0; index < 16; index++) {
    const row = document.createElement('div');
    row.className = 'channel-row';
    row.dataset.channel = String(index);
    const label = document.createElement('label');
    label.htmlFor = `channel-range-${index}`;
    label.textContent = `CH${index + 1}`;
    const range = document.createElement('input');
    range.id = `channel-range-${index}`;
    range.type = 'range';
    range.min = '1000';
    range.max = '2000';
    range.step = '1';
    range.dataset.channel = String(index);
    const number = document.createElement('input');
    number.type = 'number';
    number.min = '1000';
    number.max = '2000';
    number.step = '1';
    number.inputMode = 'numeric';
    number.dataset.channel = String(index);
    number.setAttribute('aria-label', `CH${index + 1} 脉宽`);
    const output = document.createElement('output');
    output.htmlFor = `${range.id}`;
    row.append(label, range, number, output);
    els.channelGrid.append(row);
  }
}

function renderChannels() {
  const channels = state.channels || {};
  const values = Array.isArray(channels.values) && channels.values.length === 16 ? channels.values : SAFE_CHANNEL_VALUES;
  const online = state.connMode === 'online';
  els.channelsPanel.classList.toggle('output-on', !!channels.enabled);
  els.channelStatus.classList.toggle('on', !!channels.enabled);
  els.channelStatus.textContent = channels.enabled
    ? `输出中 · ${channels.rateHz || 50} Hz · ${channels.sentFrames || 0} 帧`
    : '输出关闭';
  els.channelOutput.disabled = !online;
  els.channelOutput.textContent = channels.enabled
    ? '立即停止输出'
    : state.link?.connectedBit
      ? '启用通道输出'
      : '启用输出并建立 RF 链路';
  els.channelOutput.classList.toggle('primary', !channels.enabled && online);
  els.channelOutput.classList.toggle('danger', !!channels.enabled);
  els.safeChannels.disabled = !online;
  els.centerChannels.disabled = !online;
  els.channelNotice.classList.toggle('active', !!channels.enabled);
  els.channelNotice.textContent = channels.enabled
    ? '正在持续发送 RC 指令。页面连接或串口断开时，服务端会立即自动停发。'
    : channels.stopReason
      ? `${channels.stopReason}；RF 链路随后会停止。启用前请移除桨叶并确认通道映射。`
      : '输出默认关闭，因此不会维持 RF 链路。安全初值为 CH1/2/4 居中、CH3 与 CH5–16 最低；启用前请移除桨叶并确认通道映射。';

  els.channelGrid.querySelectorAll('.channel-row').forEach((row, index) => {
    const value = Math.max(1000, Math.min(2000, Number(values[index]) || 1000));
    const range = row.querySelector('input[type="range"]');
    const number = row.querySelector('input[type="number"]');
    const output = row.querySelector('output');
    range.disabled = !online;
    number.disabled = !online;
    if (document.activeElement !== range) range.value = String(value);
    if (document.activeElement !== number) number.value = String(value);
    output.textContent = `${value} µs · ${channelPercent(value)}`;
  });
  if (state.link) renderLink(state.link);
}

function setLocalChannel(index, rawValue) {
  const value = Math.round(Number(rawValue));
  if (!Number.isFinite(value) || value < 1000 || value > 2000) return false;
  const values = [...(state.channels.values || SAFE_CHANNEL_VALUES)];
  values[index] = value;
  state.channels = { ...state.channels, values };
  const row = els.channelGrid.querySelector(`.channel-row[data-channel="${index}"]`);
  if (row) {
    row.querySelector('input[type="range"]').value = String(value);
    row.querySelector('input[type="number"]').value = String(value);
    row.querySelector('output').textContent = `${value} µs · ${channelPercent(value)}`;
  }
  clearTimeout(channelSendTimer);
  channelSendTimer = setTimeout(() => send({ type: 'channels-set', values }), 25);
  return true;
}

async function refreshPorts() {
  try {
    const resp = await fetch('/api/ports');
    const data = await resp.json();
    state.ports = data.ports || [];
    const old = els.port.value;
    els.port.replaceChildren();
    for (const port of state.ports) {
      const opt = document.createElement('option');
      opt.value = port.path;
      opt.textContent = port.label || port.path;
      els.port.append(opt);
    }
    const sessionPath = normalizeSessionPath(state.path);
    if (sessionPath && state.ports.some((p) => p.path === sessionPath)) els.port.value = sessionPath;
    else if (old && state.ports.some((p) => p.path === old)) els.port.value = old;
  } catch (err) {
    logLine({ level: 'error', text: `刷新串口失败: ${err.message}` });
  }
}

els.refresh.onclick = refreshPorts;
els.connect.onclick = () => {
  if (state.connMode !== 'closed') {
    send({ type: 'close' });
  } else {
    const path = els.port.value;
    if (!path) return;
    logLine({ level: 'info', text: path === '__demo__' ? '打开演示模拟器' : `打开 ${path}` });
    send({ type: 'open', path });
  }
};
els.reload.onclick = () => send({ type: 'reload' });
els.safeChannels.onclick = () => send({ type: 'channels-reset', profile: 'safe' });
els.centerChannels.onclick = () => send({ type: 'channels-reset', profile: 'center' });
els.channelOutput.onclick = () => {
  if (state.channels.enabled) {
    send({ type: 'channels-enable', enabled: false });
    return;
  }
  const confirmed = window.confirm(
    '确认启用全部 16 个 RC 通道的连续输出？\n\n请先移除桨叶、固定载具，并确认 CH3/油门和 CH5/解锁映射。',
  );
  if (confirmed) send({ type: 'channels-enable', enabled: true });
};

els.channelGrid.oninput = (event) => {
  const input = event.target.closest('input[data-channel]');
  if (!input) return;
  const index = Number(input.dataset.channel);
  if (input.type === 'range') setLocalChannel(index, input.value);
  else if (input.checkValidity() && input.value !== '') setLocalChannel(index, input.value);
};

els.channelGrid.onchange = (event) => {
  const input = event.target.closest('input[type="number"][data-channel]');
  if (!input) return;
  if (!input.checkValidity() || !setLocalChannel(Number(input.dataset.channel), input.value)) {
    input.reportValidity();
    renderChannels();
  }
};

els.tree.onclick = (event) => {
  const btn = event.target.closest('[data-action]');
  const folder = event.target.closest('[data-folder]');
  if (folder) {
    const id = Number(folder.dataset.id);
    if (state.collapsed.has(id)) state.collapsed.delete(id);
    else state.collapsed.add(id);
    renderTree();
    return;
  }
  if (!btn) return;
  const id = Number(btn.dataset.field);
  const action = btn.dataset.action;
  const field = state.fields.get(id);
  if (!field) return;
  if (action === 'edit') {
    const value = displayedValue(field);
    state.drafts.set(id, { value, original: value });
    renderTree();
  } else if (action === 'cancel-draft') {
    state.drafts.delete(id);
    renderTree();
  } else if (action === 'save-draft') {
    const input = els.tree.querySelector(`input[data-field="${id}"]`);
    if (!input || !input.checkValidity()) {
      input?.reportValidity();
      return;
    }
    const editedValue = Number(input.value);
    if (!Number.isFinite(editedValue)) return;
    const value = wireValue(field, editedValue);
    state.drafts.delete(id);
    send({ type: 'save', fieldId: id, value });
  } else if (['1', '4', '5', '6'].includes(action)) {
    send({ type: 'command', fieldId: id, action: Number(action) });
  }
};

els.tree.oninput = (event) => {
  const input = event.target;
  if (input.matches('input[data-field]')) {
    const id = Number(input.dataset.field);
    const field = state.fields.get(id);
    if (field) state.drafts.set(id, { value: Number(input.value), original: field.value });
  }
};

els.tree.onchange = (event) => {
  const sel = event.target;
  if (sel.matches('select[data-field]')) {
    const id = Number(sel.dataset.field);
    send({ type: 'save', fieldId: id, value: Number(sel.value) });
  }
};

initChannelGrid();
renderChannels();
renderTelemetry();
connectWs();
refreshPorts();
