"use strict";

const POLL_INTERVAL_MS = 250;
const REQUEST_TIMEOUT_MS = 2200;
const SNAPSHOT_STALE_MS = 1500;
const LINK_METRICS_STALE_MS = 1500;
const NODE_COUNT = 8;
const RSSI_UNKNOWN = -127;

const elements = {};
const nodeViews = new Map();
const nodeDrafts = new Map();
const nodeRequestVersions = new Map();

let latestState = null;
let stateRequestInFlight = false;
let commandInFlight = false;
let consecutivePollErrors = 0;
let stateGeneration = 0;
let serviceReachable = false;
let toastTimer = null;

const STATUS_BITS = [
  { bit: 0, label: "链路保护", tone: "danger" },
  { bit: 1, label: "Tach 超时", tone: "warning" },
  { bit: 2, label: "疑似堵转", tone: "danger" },
  { bit: 3, label: "序号跳变", tone: "warning" },
  { bit: 4, label: "射频错误", tone: "danger" },
  { bit: 5, label: "配置错误", tone: "danger" },
  { bit: 7, label: "遥测陈旧", tone: "warning" },
];

const DIAGNOSTIC_LABELS = {
  accepted_replies: "有效回复",
  begin_ok: "射频初始化",
  begin_error: "初始化错误码",
  driver_error: "驱动错误码",
  fault: "最后故障",
  fault_code: "故障编号",
  frame_sequence: "诊断帧序号",
  invalid_replies: "无效回复",
  last_rx_error: "最近接收错误",
  last_tx_error: "最近发送错误",
  node_id: "关联节点",
  receive_crc_errors: "接收 CRC 错",
  receive_io_errors: "接收 I/O 错",
  receive_timeouts: "接收超时",
  sequence: "关联序号",
  transmit_failures: "发送失败",
  unknown_frames: "未知帧",
  status_decode_errors: "快照解码错误",
  link_metrics_decode_errors: "RSSI 快照解码错误",
};

const DIAGNOSTIC_ORDER = [
  "begin_ok",
  "fault",
  "driver_error",
  "last_tx_error",
  "last_rx_error",
  "accepted_replies",
  "receive_timeouts",
  "receive_crc_errors",
  "receive_io_errors",
  "transmit_failures",
  "invalid_replies",
  "node_id",
  "sequence",
  "unknown_frames",
  "status_decode_errors",
  "link_metrics_decode_errors",
];

document.addEventListener("DOMContentLoaded", init);

function init() {
  cacheElements();
  createNodeRows();
  bindEvents();
  setControlAvailability(false);
  void loadPorts();
  void pollState();
  window.setInterval(() => void pollState(), POLL_INTERVAL_MS);
  window.setInterval(() => {
    if (!isConnected(latestState)) {
      void loadPorts({ quiet: true });
    }
  }, 5000);
}

function cacheElements() {
  const ids = [
    "connectionState",
    "connectionStateText",
    "portSelect",
    "connectButton",
    "disconnectButton",
    "notice",
    "noticeText",
    "activePort",
    "lastUpdated",
    "failsafeCard",
    "failsafeValue",
    "failsafeDetail",
    "onlineCount",
    "onlineDetail",
    "uptimeValue",
    "nextPollValue",
    "frameAgeValue",
    "parserRejectedValue",
    "allDutyForm",
    "allDutyRange",
    "allDutyNumber",
    "allDutyPercent",
    "fullSpeedButton",
    "diagnosticHeadline",
    "diagnosticList",
    "nodeRows",
    "nodeRowTemplate",
    "toast",
  ];

  ids.forEach((id) => {
    elements[id] = document.getElementById(id);
  });
}

function createNodeRows() {
  for (let nodeId = 1; nodeId <= NODE_COUNT; nodeId += 1) {
    const row = elements.nodeRowTemplate.content.firstElementChild.cloneNode(true);
    const range = row.querySelector(".node-duty-range");
    const number = row.querySelector(".node-duty-number");

    row.dataset.nodeId = String(nodeId);
    row.dataset.online = "false";
    row.dataset.fault = "false";
    row.querySelector(".node-name").textContent = `节点 ${String(nodeId).padStart(2, "0")}`;
    range.dataset.nodeId = String(nodeId);
    number.dataset.nodeId = String(nodeId);
    range.setAttribute("aria-label", `节点 ${nodeId} duty 滑块`);
    number.setAttribute("aria-label", `节点 ${nodeId} duty 数值`);

    const view = {
      row,
      presence: row.querySelector(".node-presence"),
      onlineText: row.querySelector(".node-online-text"),
      rpm: row.querySelector(".rpm-value"),
      uplinkRssi: row.querySelector(".uplink-rssi-value"),
      downlinkRssi: row.querySelector(".downlink-rssi-value"),
      range,
      number,
      dutyPercent: row.querySelector(".node-duty-percent"),
      saveState: row.querySelector(".node-save-state"),
      applied: row.querySelector(".applied-duty"),
      meter: row.querySelector(".mini-meter i"),
      age: row.querySelector(".age-value"),
      missed: row.querySelector(".missed-value"),
      tags: row.querySelector(".status-tags"),
    };

    range.addEventListener("input", () => previewNodeDuty(nodeId, range.value, "range"));
    range.addEventListener("change", () => void commitNodeDuty(nodeId, range.value));
    number.addEventListener("input", () => previewNodeDuty(nodeId, number.value, "number"));
    number.addEventListener("change", () => void commitNodeDuty(nodeId, number.value));
    number.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        number.blur();
      }
    });

    nodeViews.set(nodeId, view);
    elements.nodeRows.appendChild(row);
  }
}

function bindEvents() {
  elements.connectButton.addEventListener("click", () => void connectPort());
  elements.disconnectButton.addEventListener("click", () => void disconnectPort());

  elements.allDutyRange.addEventListener("input", () => {
    setGlobalDutyPreview(elements.allDutyRange.value, "range");
  });
  elements.allDutyNumber.addEventListener("input", () => {
    setGlobalDutyPreview(elements.allDutyNumber.value, "number");
  });
  elements.allDutyForm.addEventListener("submit", (event) => {
    event.preventDefault();
    void applyDutyToAll(elements.allDutyNumber.value);
  });
  elements.fullSpeedButton.addEventListener("click", () => void requestFullSpeed());
}

async function requestJson(path, options = {}) {
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  const init = {
    method: options.method || "GET",
    headers: { Accept: "application/json", ...(options.headers || {}) },
    signal: controller.signal,
  };

  if (options.body !== undefined) {
    init.headers["Content-Type"] = "application/json";
    init.body = JSON.stringify(options.body);
  }

  try {
    const response = await fetch(path, init);
    const raw = await response.text();
    let payload = {};

    if (raw) {
      try {
        payload = JSON.parse(raw);
      } catch {
        const error = new Error(`服务返回了非 JSON 响应（HTTP ${response.status}）`);
        error.serviceUnavailable = true;
        throw error;
      }
    }

    if (!response.ok) {
      const detail = payload && (payload.error || payload.message);
      const detailMessage = detail && typeof detail === "object" ? detail.message : detail;
      const error = new Error(detailMessage || `请求失败（HTTP ${response.status}）`);
      error.serviceUnavailable = false;
      throw error;
    }
    return payload;
  } catch (error) {
    if (error && error.name === "AbortError") {
      const timeoutError = new Error("本地服务响应超时");
      timeoutError.serviceUnavailable = true;
      throw timeoutError;
    }
    if (error && typeof error.serviceUnavailable === "boolean") throw error;
    const serviceError = new Error(messageFrom(error));
    serviceError.serviceUnavailable = true;
    throw serviceError;
  } finally {
    window.clearTimeout(timeout);
  }
}

async function loadPorts({ quiet = false } = {}) {
  const previous = elements.portSelect.value;
  try {
    const payload = await requestJson("/api/ports");
    const ports = Array.isArray(payload.ports) ? payload.ports : [];
    renderPorts(ports, previous);
  } catch (error) {
    if (!quiet) {
      renderPorts([], previous);
      showToast(`无法读取串口：${messageFrom(error)}`, "danger");
    }
  }
}

function renderPorts(ports, preferredValue) {
  const connectedPort = latestState && typeof latestState.port === "string" ? latestState.port : "";
  const wanted = connectedPort || preferredValue;
  const fragment = document.createDocumentFragment();
  const knownPaths = new Set();

  if (ports.length === 0 && !wanted) {
    fragment.appendChild(new Option("未发现串口", ""));
  } else {
    fragment.appendChild(new Option("选择串口…", ""));
    ports.forEach((port) => {
      const path = typeof port === "string" ? port : String(port.device || port.path || "");
      if (!path || knownPaths.has(path)) return;
      knownPaths.add(path);
      const label = typeof port === "string"
        ? port
        : String(port.description || port.label || port.device || path);
      const suffix = label === path ? "" : ` · ${path}`;
      const option = new Option(`${label}${suffix}`, path);
      if (port && typeof port === "object" && port.hwid) option.title = String(port.hwid);
      fragment.appendChild(option);
    });
  }

  if (wanted && !knownPaths.has(wanted)) {
    fragment.appendChild(new Option(`${wanted} · 当前`, wanted));
  }

  elements.portSelect.replaceChildren(fragment);
  if (wanted) elements.portSelect.value = wanted;
}

async function pollState() {
  if (stateRequestInFlight || commandInFlight) return;
  const generation = stateGeneration;
  stateRequestInFlight = true;

  try {
    const payload = await requestJson("/api/state");
    if (generation !== stateGeneration) return;
    consecutivePollErrors = 0;
    serviceReachable = true;
    latestState = payload && typeof payload === "object" ? payload : {};
    renderState(latestState);
  } catch (error) {
    if (generation !== stateGeneration) return;
    consecutivePollErrors += 1;
    serviceReachable = false;
    if (consecutivePollErrors === 1 || consecutivePollErrors % 20 === 0) {
      renderServiceError(error);
    }
  } finally {
    stateRequestInFlight = false;
    if (generation !== stateGeneration && !commandInFlight) void pollState();
  }
}

function renderState(state) {
  const connected = isConnected(state);
  const connecting = isConnecting(state);
  const hasSnapshot = Number.isFinite(Number(state.last_status_at)) && Number(state.last_status_at) > 0;
  const fallbackSnapshotAge = hasSnapshot ? Date.now() - Number(state.last_status_at) * 1000 : Infinity;
  const snapshotFresh = connected && hasSnapshot && (
    typeof state.status_snapshot_fresh === "boolean"
      ? state.status_snapshot_fresh
      : fallbackSnapshotAge <= SNAPSHOT_STALE_MS
  );
  const hasLinkMetrics = Number.isFinite(Number(state.last_link_metrics_at))
    && Number(state.last_link_metrics_at) > 0;
  const fallbackLinkMetricsAge = hasLinkMetrics
    ? Date.now() - Number(state.last_link_metrics_at) * 1000
    : Infinity;
  const linkMetricsFresh = hasLinkMetrics && (
    typeof state.link_metrics_snapshot_fresh === "boolean"
      ? state.link_metrics_snapshot_fresh
      : fallbackLinkMetricsAge <= LINK_METRICS_STALE_MS
  );
  const snapshot = snapshotFresh && Array.isArray(state.nodes) ? state : null;
  const nodes = Array.isArray(state.nodes) ? state.nodes : [];
  const desiredDuty = Array.isArray(state.duties) ? state.duties : [];
  const stateError = state.last_error ? String(state.last_error) : "";

  if (!connected) {
    // The backend always resets to full speed when a session ends.  Do not
    // leave optimistic browser drafts looking like active lower-duty commands.
    nodeDrafts.clear();
    nodeRequestVersions.clear();
    setGlobalDutyPreview(clampInteger(desiredDuty[0], 0, 255, 255));
  }

  elements.connectionState.dataset.state = stateError ? "error" : connected ? "connected" : connecting ? "loading" : "disconnected";
  elements.connectionStateText.textContent = stateError
    ? "连接异常"
    : connected
      ? state.demo
        ? "演示数据已连接"
        : "串口已连接"
      : connecting
        ? "正在连接"
        : "串口未连接";
  elements.activePort.textContent = state.port ? String(state.port) : "未连接";
  elements.lastUpdated.textContent = `状态更新 ${formatClock(new Date())}`;

  elements.connectButton.hidden = connected;
  elements.disconnectButton.hidden = !connected;
  elements.connectButton.disabled = commandInFlight || connecting;
  elements.disconnectButton.disabled = commandInFlight;
  elements.portSelect.disabled = commandInFlight || connected || connecting;
  setControlAvailability(connected && !commandInFlight);

  if (state.port && !Array.from(elements.portSelect.options).some((option) => option.value === state.port)) {
    elements.portSelect.appendChild(new Option(`${state.port} · 当前`, state.port));
  }
  if (state.port) elements.portSelect.value = state.port;

  renderNotice({ connected, connecting, error: stateError });
  renderSnapshot(snapshot, nodes, hasSnapshot);
  renderDiagnostics(state.diagnostic, state);
  elements.frameAgeValue.textContent = formatTimestampAge(state.last_status_at);
  elements.parserRejectedValue.textContent = formatInteger(state.serial_rejected_frames);

  const nodeById = new Map();
  nodes.forEach((node, index) => {
    const id = clampInteger(node && node.node_id, 1, NODE_COUNT, index + 1);
    if (!nodeById.has(id)) nodeById.set(id, node || {});
  });

  for (let nodeId = 1; nodeId <= NODE_COUNT; nodeId += 1) {
    renderNode(
      nodeId,
      nodeById.get(nodeId),
      desiredDuty[nodeId - 1],
      connected,
      snapshotFresh,
      connected && linkMetricsFresh && nodeIsOnline(nodeById.get(nodeId), snapshotFresh),
    );
  }
}

function renderNotice({ connected, connecting, error }) {
  if (error) {
    elements.notice.dataset.tone = "danger";
    elements.noticeText.textContent = `${error}。控制命令可能未送达；请按实机 failsafe 状态处理。`;
  } else if (connecting) {
    elements.notice.dataset.tone = "warning";
    elements.noticeText.textContent = "正在打开串口，连接完成前不会发送 duty 命令。";
  } else if (connected) {
    elements.notice.dataset.tone = "success";
    elements.noticeText.textContent = "本地串口已连接。面板状态来自后端快照，不能替代风扇转动、波形或温度的现场确认。";
  } else {
    elements.notice.dataset.tone = "warning";
    elements.noticeText.textContent = "串口未连接，控制操作已锁定；风扇应由 TX/RX failsafe 转入全速，仍需现场确认。";
  }
}

function renderSnapshot(snapshot, nodes, hasSnapshot) {
  if (!snapshot) {
    elements.failsafeCard.dataset.level = "unknown";
    elements.failsafeValue.textContent = "未知";
    elements.failsafeDetail.textContent = hasSnapshot ? "状态快照已过期" : "等待状态快照";
    elements.onlineCount.textContent = "—";
    elements.onlineDetail.textContent = hasSnapshot ? "最近遥测已过期" : "尚无节点遥测";
    elements.uptimeValue.textContent = "—";
    elements.nextPollValue.textContent = "—";
    return;
  }

  const failsafe = Boolean(snapshot.host_failsafe);
  const online = nodes.reduce((count, node) => count + (nodeIsOnline(node) ? 1 : 0), 0);
  const degraded = nodes.filter((node) => nodeIsOnline(node) && nodeTelemetryStale(node)).length;
  elements.failsafeCard.dataset.level = failsafe ? "danger" : "safe";
  elements.failsafeValue.textContent = failsafe ? "已触发" : "正常";
  elements.failsafeDetail.textContent = failsafe ? "TX 应广播全部 duty=255" : "Host watchdog 当前健康";
  elements.onlineCount.textContent = String(online);
  elements.onlineDetail.textContent = degraded > 0
    ? `${degraded} 个节点等待回传 · ${NODE_COUNT - online} 个离线`
    : online === NODE_COUNT ? "全部节点有新鲜回传" : `${NODE_COUNT - online} 个节点离线`;
  elements.uptimeValue.textContent = formatDuration(snapshot.uptime_seconds);
  elements.nextPollValue.textContent = formatNodeId(snapshot.next_poll_id);
}

function renderDiagnostics(diagnostic, state = {}) {
  elements.diagnosticList.replaceChildren();

  if (diagnostic === null || diagnostic === undefined || diagnostic === "") {
    elements.diagnosticHeadline.textContent = "暂无数据";
    appendDiagnostic("状态", "等待后端诊断");
    appendDiagnostic("未知帧", formatInteger(state.unknown_frames));
    appendDiagnostic("快照解码错误", formatInteger(state.status_decode_errors));
    appendDiagnostic("RSSI 快照解码错误", formatInteger(state.link_metrics_decode_errors));
    return;
  }

  if (typeof diagnostic !== "object") {
    const value = String(diagnostic);
    elements.diagnosticHeadline.textContent = value;
    appendDiagnostic("最近诊断", value);
    return;
  }

  const enriched = {
    ...diagnostic,
    unknown_frames: state.unknown_frames,
    status_decode_errors: state.status_decode_errors,
    link_metrics_decode_errors: state.link_metrics_decode_errors,
  };
  const entries = DIAGNOSTIC_ORDER
    .filter((key) => enriched[key] !== null && enriched[key] !== undefined)
    .map((key) => [key, enriched[key]]);
  const fault = diagnostic.fault;
  const driverError = Number(diagnostic.driver_error);
  if (fault && fault !== "none") {
    elements.diagnosticHeadline.textContent = `${formatFault(fault)}${Number.isFinite(driverError) ? ` · ${driverError}` : ""}`;
  } else if (diagnostic.begin_ok === false) {
    elements.diagnosticHeadline.textContent = "射频初始化失败";
  } else {
    elements.diagnosticHeadline.textContent = "射频运行中";
  }

  if (entries.length === 0) {
    appendDiagnostic("状态", "无诊断字段");
    return;
  }

  entries.slice(0, 12).forEach(([key, value]) => {
    let printable = printableValue(value);
    if (key === "fault") printable = formatFault(value);
    if (key === "begin_ok") printable = value ? "成功" : "失败";
    appendDiagnostic(DIAGNOSTIC_LABELS[key] || humanizeKey(key), printable);
  });
}

function appendDiagnostic(label, value) {
  const wrapper = document.createElement("div");
  const term = document.createElement("dt");
  const detail = document.createElement("dd");
  term.textContent = label;
  detail.textContent = value;
  detail.title = value;
  wrapper.append(term, detail);
  elements.diagnosticList.appendChild(wrapper);
}

function renderNode(nodeId, node, desiredValue, connected, snapshotFresh, linkMetricsFresh) {
  const view = nodeViews.get(nodeId);
  if (!view) return;

  const hasTelemetry = Boolean(node);
  const online = connected && nodeIsOnline(node, snapshotFresh);
  const stale = !connected || nodeTelemetryStale(node, snapshotFresh);
  const status = clampInteger(node && node.status, 0, 255, 0);
  const desired = resolveDesiredDuty(nodeId, desiredValue);
  const applied = hasTelemetry && !stale ? clampInteger(node.applied_duty, 0, 255, null) : null;

  view.row.dataset.online = String(online);
  view.row.dataset.fault = String(status !== 0 || stale);
  view.onlineText.textContent = online ? stale ? "等待回传 / 降级" : "遥测在线" : hasTelemetry ? "遥测离线" : "尚无遥测";
  view.rpm.textContent = hasTelemetry && !stale ? formatInteger(node.rpm) : "—";
  view.uplinkRssi.textContent = linkMetricsFresh && !stale ? formatRssi(node.uplink_rssi_dbm) : "—";
  view.downlinkRssi.textContent = linkMetricsFresh && !stale ? formatRssi(node.downlink_rssi_dbm) : "—";
  setNodeDutyInputs(view, desired);
  view.range.disabled = !connected || commandInFlight;
  view.number.disabled = !connected || commandInFlight;
  view.applied.textContent = applied === null ? "—" : String(applied);
  view.meter.style.width = applied === null ? "0%" : `${dutyPercent(applied)}%`;
  view.age.textContent = hasTelemetry ? formatTelemetryAge(node.age_100ms) : "—";
  view.missed.textContent = hasTelemetry ? formatInteger(node.missed_polls) : "—";
  renderStatusTags(view.tags, node, status, online, stale);
}

function renderStatusTags(container, node, status, online, stale) {
  container.replaceChildren();
  let labels = Array.isArray(node && node.status_labels)
    ? node.status_labels.filter((label) => label !== null && label !== undefined).map(String)
    : [];

  if (labels.length === 0 && status !== 0) {
    labels = STATUS_BITS.filter((item) => status & (1 << item.bit)).map((item) => item.label);
  }

  if (labels.length === 0) {
    const healthy = online && !stale;
    appendStatusTag(container, healthy ? "正常" : online ? "等待回传" : "无新鲜回传", healthy ? "ok" : "warning");
    return;
  }

  labels.forEach((label) => {
    const known = STATUS_BITS.find((item) => item.label === label || label.toUpperCase().includes(statusKeyForBit(item.bit)));
    appendStatusTag(container, localizeStatusLabel(label), known ? known.tone : "warning");
  });
}

function appendStatusTag(container, label, tone) {
  const tag = document.createElement("span");
  tag.className = `status-tag ${tone}`;
  tag.textContent = label;
  container.appendChild(tag);
}

function resolveDesiredDuty(nodeId, backendValue) {
  const draft = nodeDrafts.get(nodeId);
  const backend = clampInteger(backendValue, 0, 255, 255);

  if (!draft) return backend;
  if (!draft.pending && backend === draft.value) {
    nodeDrafts.delete(nodeId);
    return backend;
  }
  if (!draft.pending && Date.now() > draft.keepUntil) {
    nodeDrafts.delete(nodeId);
    return backend;
  }
  return draft.value;
}

function setNodeDutyInputs(view, value) {
  const active = document.activeElement;
  if (active !== view.range && active !== view.number) {
    view.range.value = String(value);
    view.number.value = String(value);
  }
  view.dutyPercent.textContent = `${dutyPercent(value)}%`;
  view.range.setAttribute("aria-valuetext", `${value}，${dutyPercent(value)}%`);
}

function previewNodeDuty(nodeId, rawValue, source) {
  const view = nodeViews.get(nodeId);
  if (!view) return;
  const value = parseDuty(rawValue);
  if (value === null) {
    view.saveState.dataset.state = "error";
    view.saveState.textContent = "请输入 0–255 整数";
    return;
  }
  nodeDrafts.set(nodeId, { value, pending: false, keepUntil: Date.now() + 3000 });
  if (source !== "range") view.range.value = String(value);
  if (source !== "number") view.number.value = String(value);
  view.dutyPercent.textContent = `${dutyPercent(value)}%`;
  view.saveState.dataset.state = "";
  view.saveState.textContent = "待发送";
}

async function commitNodeDuty(nodeId, rawValue) {
  const view = nodeViews.get(nodeId);
  if (!view) return;
  const duty = parseDuty(rawValue);
  if (duty === null) {
    nodeDrafts.delete(nodeId);
    const fallback = Array.isArray(latestState && latestState.duties)
      ? latestState.duties[nodeId - 1]
      : 255;
    setNodeDutyInputs(view, clampInteger(fallback, 0, 255, 255));
    view.saveState.dataset.state = "error";
    view.saveState.textContent = "输入无效";
    showToast(`节点 ${nodeId} duty 必须是 0–255 的整数，未发送。`, "danger");
    return;
  }

  if (!isConnected(latestState)) {
    showToast("串口未连接，节点 duty 未发送。", "danger");
    return;
  }

  const version = (nodeRequestVersions.get(nodeId) || 0) + 1;
  nodeRequestVersions.set(nodeId, version);
  nodeDrafts.set(nodeId, { value: duty, pending: true, keepUntil: Number.POSITIVE_INFINITY });
  view.saveState.dataset.state = "pending";
  view.saveState.textContent = "发送中…";

  try {
    await requestJson("/api/duty/node", { method: "POST", body: { node: nodeId, duty } });
    if (nodeRequestVersions.get(nodeId) !== version) return;
    nodeDrafts.set(nodeId, { value: duty, pending: false, keepUntil: Date.now() + 1500 });
    view.saveState.dataset.state = "sent";
    view.saveState.textContent = "已发送";
    window.setTimeout(() => {
      if (view.saveState.dataset.state === "sent") {
        view.saveState.dataset.state = "";
        view.saveState.textContent = "开环命令";
      }
    }, 1400);
  } catch (error) {
    if (nodeRequestVersions.get(nodeId) !== version) return;
    const draft = nodeDrafts.get(nodeId);
    if (draft) {
      draft.pending = false;
      draft.keepUntil = Date.now();
    }
    view.saveState.dataset.state = "error";
    view.saveState.textContent = "发送失败";
    if (error && error.serviceUnavailable) renderServiceError(error);
    showToast(`节点 ${nodeId} duty 未发送：${messageFrom(error)}`, "danger");
  }
}

function setGlobalDutyPreview(rawValue, source = "both") {
  const duty = parseDuty(rawValue);
  if (duty === null) {
    elements.allDutyPercent.textContent = "请输入 0–255 整数";
    return;
  }
  if (source !== "number") elements.allDutyNumber.value = String(duty);
  if (source !== "range") elements.allDutyRange.value = String(duty);
  elements.allDutyPercent.textContent = `${dutyPercent(duty)}%`;
  elements.allDutyRange.setAttribute("aria-valuetext", `${duty}，${dutyPercent(duty)}%`);
}

async function applyDutyToAll(rawValue) {
  const duty = parseDuty(rawValue);
  if (duty === null) {
    showToast("全局 duty 必须是 0–255 的整数，未发送。", "danger");
    elements.allDutyNumber.focus();
    return;
  }
  if (!isConnected(latestState)) {
    showToast("串口未连接，全局 duty 未发送。", "danger");
    return;
  }

  await runCommand(
    () => requestJson("/api/duty/all", { method: "POST", body: { duty } }),
    `已发送全部节点 duty=${duty}（${dutyPercent(duty)}%）`,
    "全局 duty 未发送",
    () => setAllNodeDrafts(duty),
  );
}

async function requestFullSpeed() {
  if (!isConnected(latestState)) {
    showToast("串口未连接，无法通过面板发送全速命令；请确认硬件 failsafe。", "danger");
    return;
  }

  await runCommand(
    () => requestJson("/api/full", { method: "POST", body: {} }),
    "紧急全速命令已发送：全部 duty=255",
    "紧急全速命令未送达",
    () => {
      setGlobalDutyPreview(255);
      setAllNodeDrafts(255);
    },
  );
}

function setAllNodeDrafts(duty) {
  for (let nodeId = 1; nodeId <= NODE_COUNT; nodeId += 1) {
    nodeDrafts.set(nodeId, { value: duty, pending: false, keepUntil: Date.now() + 1800 });
    const view = nodeViews.get(nodeId);
    if (view) setNodeDutyInputs(view, duty);
  }
}

async function connectPort() {
  const port = elements.portSelect.value;
  if (!port) {
    showToast("请先选择 FanLink TX 串口。", "danger");
    elements.portSelect.focus();
    return;
  }

  await runCommand(
    () => requestJson("/api/connect", { method: "POST", body: { port } }),
    `正在连接 ${port}`,
    "串口连接失败",
    () => void pollState(),
  );
}

async function disconnectPort() {
  await runCommand(
    () => requestJson("/api/disconnect", { method: "POST", body: {} }),
    "串口已断开；请确认 TX/RX 已按 watchdog 转入全速保护",
    "串口断开请求失败",
    () => void pollState(),
  );
}

async function runCommand(action, successMessage, failurePrefix, onSuccess) {
  if (commandInFlight) return;
  stateGeneration += 1;
  commandInFlight = true;
  renderCommandBusy(true);
  try {
    const payload = await action();
    if (payload && payload.state && typeof payload.state === "object") {
      serviceReachable = true;
      latestState = payload.state;
      renderState(latestState);
    }
    if (onSuccess) onSuccess();
    showToast(successMessage, "success");
  } catch (error) {
    if (error && error.serviceUnavailable) renderServiceError(error);
    showToast(`${failurePrefix}：${messageFrom(error)}`, "danger");
  } finally {
    commandInFlight = false;
    renderCommandBusy(false);
    void pollState();
  }
}

function renderCommandBusy(busy) {
  const connected = isConnected(latestState);
  elements.connectButton.disabled = busy || !serviceReachable || isConnecting(latestState);
  elements.disconnectButton.disabled = busy || !serviceReachable;
  elements.portSelect.disabled = busy || !serviceReachable || connected || isConnecting(latestState);
  setControlAvailability(connected && serviceReachable && !busy);
}

function setControlAvailability(enabled) {
  elements.allDutyRange.disabled = !enabled;
  elements.allDutyNumber.disabled = !enabled;
  elements.fullSpeedButton.disabled = !enabled;
  elements.allDutyForm.querySelector("button[type='submit']").disabled = !enabled;
  nodeViews.forEach((view) => {
    view.range.disabled = !enabled;
    view.number.disabled = !enabled;
  });
}

function renderServiceError(error) {
  // Once the service is unreachable none of its previous telemetry remains
  // live evidence.  Clear the cached session instead of leaving an old
  // "watchdog healthy" card or applied duty visible beside the error banner.
  nodeDrafts.clear();
  nodeRequestVersions.clear();
  nodeViews.forEach((view) => {
    view.saveState.dataset.state = "";
    view.saveState.textContent = "开环命令";
  });
  latestState = {
    connected: false,
    connecting: false,
    port: null,
    duties: Array(NODE_COUNT).fill(255),
    host_failsafe: true,
    nodes: [],
    diagnostic: null,
    last_status_at: null,
    serial_rejected_frames: 0,
    unknown_frames: 0,
    status_decode_errors: 0,
    last_link_metrics_at: null,
    link_metrics_snapshot_fresh: false,
    link_metrics_decode_errors: 0,
  };
  serviceReachable = false;
  renderState(latestState);
  elements.connectionState.dataset.state = "error";
  elements.connectionStateText.textContent = "本地服务不可达";
  elements.notice.dataset.tone = "danger";
  elements.noticeText.textContent = `${messageFrom(error)}。控制已锁定；请确认风扇的硬件和固件 failsafe。`;
  elements.lastUpdated.textContent = "状态轮询中断";
  elements.connectButton.disabled = true;
  elements.disconnectButton.disabled = true;
  elements.portSelect.disabled = true;
  setControlAvailability(false);
}

function isConnected(state) {
  if (!state || typeof state !== "object") return false;
  if (typeof state.connected === "boolean") return state.connected;
  const mode = String(state.mode || "").toLowerCase();
  if (["connected", "ready", "open", "running", "active"].includes(mode)) return true;
  if (["disconnected", "closed", "idle", "error", "connecting", "opening"].includes(mode)) return false;
  return Boolean(state.port && !state.error);
}

function isConnecting(state) {
  if (!state || typeof state !== "object") return false;
  if (typeof state.connecting === "boolean") return state.connecting;
  return ["connecting", "opening"].includes(String(state.mode || "").toLowerCase());
}

function nodeIsOnline(node, snapshotFresh = true) {
  if (!snapshotFresh) return false;
  if (!node || typeof node !== "object") return false;
  if (typeof node.online === "boolean") return node.online;
  // Older backends cannot provide the recovery latch: use their conservative
  // fresh-only result rather than synthesizing redundant presence in the UI.
  return !nodeTelemetryStale(node, snapshotFresh);
}

function nodeTelemetryStale(node, snapshotFresh = true) {
  if (!snapshotFresh || !node || typeof node !== "object") return true;
  const age = clampInteger(node.age_100ms, 0, 255, 255);
  const effectiveAge = typeof node.effective_age_seconds === "number"
    ? node.effective_age_seconds : age * 0.1;
  return Boolean(node.stale) || age === 255 || effectiveAge >= 2.5
    || Boolean(clampInteger(node.status, 0, 255, 0) & 0x80);
}

function clampInteger(value, min, max, fallback) {
  if (value === null || value === undefined || value === "") return fallback;
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) return fallback;
  return Math.min(max, Math.max(min, Math.round(numeric)));
}

function parseDuty(value) {
  if (value === null || value === undefined || String(value).trim() === "") return null;
  const numeric = Number(value);
  if (!Number.isInteger(numeric) || numeric < 0 || numeric > 255) return null;
  return numeric;
}

function dutyPercent(duty) {
  return Math.round((clampInteger(duty, 0, 255, 0) / 255) * 100);
}

function formatInteger(value) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? Math.max(0, Math.round(numeric)).toLocaleString("zh-CN") : "—";
}

function formatMilliseconds(value) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric) || numeric < 0) return "—";
  if (numeric < 1000) return `${Math.round(numeric)} ms`;
  return `${(numeric / 1000).toFixed(numeric < 10000 ? 1 : 0)} s`;
}

function formatTimestampAge(value) {
  const seconds = Number(value);
  if (!Number.isFinite(seconds) || seconds <= 0) return "—";
  return formatMilliseconds(Math.max(0, Date.now() - seconds * 1000));
}

function formatTelemetryAge(value) {
  const ticks = clampInteger(value, 0, 255, null);
  if (ticks === null) return "—";
  if (ticks >= 255) return "未见 / ≥25.5 s";
  const milliseconds = ticks * 100;
  return formatMilliseconds(milliseconds);
}

function formatRssi(value) {
  const numeric = Number(value);
  if (!Number.isInteger(numeric) || numeric < -128 || numeric > 127 || numeric === RSSI_UNKNOWN) {
    return "—";
  }
  return String(numeric);
}

function formatDuration(value) {
  const total = clampInteger(value, 0, Number.MAX_SAFE_INTEGER, null);
  if (total === null) return "—";
  const days = Math.floor(total / 86400);
  const hours = Math.floor((total % 86400) / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const seconds = total % 60;
  if (days > 0) return `${days}d ${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}`;
  return `${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

function formatNodeId(value) {
  const id = clampInteger(value, 1, NODE_COUNT, null);
  return id === null ? "—" : `#${String(id).padStart(2, "0")}`;
}

function formatClock(date) {
  return new Intl.DateTimeFormat("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  }).format(date);
}

function humanizeKey(key) {
  return String(key)
    .replace(/_/g, " ")
    .replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function printableValue(value) {
  if (Array.isArray(value)) return value.join(" · ") || "—";
  if (typeof value === "object" && value !== null) return JSON.stringify(value);
  if (typeof value === "boolean") return value ? "是" : "否";
  return String(value);
}

function formatFault(value) {
  const names = {
    none: "无故障",
    begin: "初始化失败",
    encode: "封包失败",
    transmit: "发送失败",
    receive_timeout: "接收超时",
    receive_crc: "接收 CRC 错",
    receive_io: "接收 I/O 错",
    invalid_reply: "无效回复",
    unknown: "未知故障",
  };
  return names[String(value)] || String(value);
}

function statusKeyForBit(bit) {
  const keys = {
    0: "LINK_FAILSAFE",
    1: "TACH_TIMEOUT",
    2: "STALL",
    3: "SEQUENCE_GAP",
    4: "RADIO_ERROR",
    5: "CONFIG_ERROR",
    7: "TELEMETRY_STALE",
  };
  return keys[bit] || "";
}

function localizeStatusLabel(label) {
  const normalized = String(label).toUpperCase();
  const known = STATUS_BITS.find((item) => normalized.includes(statusKeyForBit(item.bit)));
  return known ? known.label : String(label);
}

function messageFrom(error) {
  return error && error.message ? String(error.message) : "未知错误";
}

function showToast(message, tone = "success") {
  if (toastTimer) window.clearTimeout(toastTimer);
  elements.toast.textContent = message;
  elements.toast.dataset.tone = tone;
  elements.toast.hidden = false;
  toastTimer = window.setTimeout(() => {
    elements.toast.hidden = true;
  }, tone === "danger" ? 5200 : 3200);
}
