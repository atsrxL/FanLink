#!/usr/bin/env python3
"""Local HTTP backend for the FanLink transmitter control panel.

The backend deliberately has no web framework dependency.  It keeps all fan
commands at full speed until a local client explicitly changes them, and the
serial worker stops sending as soon as the connection is closed so the TX-side
500 ms watchdog can take over.
"""

from __future__ import annotations

import argparse
import copy
import ipaddress
import json
import math
import re
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping, Optional, Sequence
from urllib.parse import urlsplit

try:  # Works both as ``python tools/fanlink_panel.py`` and as a package import.
    from . import fanlink_cli
except ImportError:  # pragma: no cover - exercised by direct script execution.
    import fanlink_cli  # type: ignore


HTTP_PORT = 8765
SERIAL_BAUD = 115200
SEND_INTERVAL_SECONDS = 0.2
STATUS_STALE_SECONDS = 2.5
LINK_METRICS_STALE_SECONDS = 2.5
NODE_TELEMETRY_STALE_SECONDS = 2.5
NODE_OFFLINE_SECONDS = 6.0
NODE_OFFLINE_MISSED_POLLS = 5
MAX_REQUEST_BYTES = 4096
FULL_SPEED = 255
NODE_COUNT = 8

FAULT_NAMES = {
    0: "none",
    1: "begin",
    2: "encode",
    3: "transmit",
    4: "receive_timeout",
    5: "receive_crc",
    6: "receive_io",
    7: "invalid_reply",
}

LEGACY_FAULT_NAMES = {
    "none": "none",
    "begin": "begin",
    "encode": "encode",
    "tx": "transmit",
    "rxto": "receive_timeout",
    "rxcrc": "receive_crc",
    "rxio": "receive_io",
    "invalid": "invalid_reply",
}

STATIC_FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/app.css": ("app.css", "text/css; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
}


class PanelError(RuntimeError):
    """Error that can be returned to a JSON API client without a traceback."""

    def __init__(self, message: str, *, status: int = 400, code: str = "bad_request") -> None:
        super().__init__(message)
        self.status = status
        self.code = code


def validate_duty(value: object) -> int:
    if type(value) is not int or not 0 <= value <= 255:
        raise PanelError("duty must be an integer from 0 through 255")
    return value


def validate_node(value: object) -> int:
    if type(value) is not int or not 1 <= value <= NODE_COUNT:
        raise PanelError("node must be an integer from 1 through 8")
    return value


def validate_duties(value: object) -> list[int]:
    if not isinstance(value, list) or len(value) != NODE_COUNT:
        raise PanelError("duties must be an array of exactly 8 integers")
    return [validate_duty(item) for item in value]


def validate_port_name(value: object) -> str:
    if not isinstance(value, str) or not value or value != value.strip():
        raise PanelError("port must be a non-empty string without surrounding whitespace")
    if len(value) > 256 or any(ord(character) < 0x20 for character in value):
        raise PanelError("port contains invalid characters")
    return value


def _parse_integer(text: str) -> Optional[int]:
    if not re.fullmatch(r"[-+]?\d+", text):
        return None
    try:
        return int(text, 10)
    except ValueError:
        return None


def parse_radio_diagnostic(text: str) -> dict[str, Any]:
    """Parse both FanLink RF diagnostic text formats while retaining the source.

    Supported examples are the original ``key=value`` representation and the
    compact no-equals representation used by constrained TX builds::

        RF b=1 be=0 last=rxto e=-6 ...
        RF b1 B0 f4 e-6 X0 R-6 n1 s123 x0 t100 c0 i0 v0 o0
    """

    result: dict[str, Any] = {
        "raw": text,
        "format": "unknown",
        "begin_ok": None,
        "begin_error": None,
        "fault_code": None,
        "fault": None,
        "driver_error": None,
        "last_tx_error": None,
        "last_rx_error": None,
        "node_id": None,
        "sequence": None,
        "transmit_failures": None,
        "receive_timeouts": None,
        "receive_crc_errors": None,
        "receive_io_errors": None,
        "invalid_replies": None,
        "accepted_replies": None,
        "unknown_tokens": [],
    }
    tokens = text.strip().split()
    if tokens and tokens[0].upper() == "RF":
        tokens = tokens[1:]

    legacy: dict[str, str] = {}
    compact: dict[str, int] = {}
    unknown: list[str] = []
    for token in tokens:
        if "=" in token:
            key, value = token.split("=", 1)
            if key and value:
                legacy[key] = value
            else:
                unknown.append(token)
            continue
        match = re.fullmatch(r"([A-Za-z]+)([-+]?\d+)", token)
        if match is None:
            unknown.append(token)
            continue
        compact[match.group(1)] = int(match.group(2), 10)

    result["unknown_tokens"] = unknown
    if compact:
        result["format"] = "compact"
        field_map = {
            "B": "begin_error",
            "e": "driver_error",
            "X": "last_tx_error",
            "R": "last_rx_error",
            "n": "node_id",
            "s": "sequence",
            "x": "transmit_failures",
            "t": "receive_timeouts",
            "c": "receive_crc_errors",
            "i": "receive_io_errors",
            "v": "invalid_replies",
            "o": "accepted_replies",
        }
        if "b" in compact:
            result["begin_ok"] = bool(compact["b"])
        if "f" in compact:
            result["fault_code"] = compact["f"]
            result["fault"] = FAULT_NAMES.get(compact["f"], "unknown")
        for source, destination in field_map.items():
            if source in compact:
                result[destination] = compact[source]
        return result

    if legacy:
        result["format"] = "legacy"
        numeric_map = {
            "be": "begin_error",
            "e": "driver_error",
            "n": "node_id",
            "s": "sequence",
            "tx": "transmit_failures",
            "to": "receive_timeouts",
            "crc": "receive_crc_errors",
            "io": "receive_io_errors",
            "inv": "invalid_replies",
            "ok": "accepted_replies",
        }
        begin_value = _parse_integer(legacy.get("b", ""))
        if begin_value is not None:
            result["begin_ok"] = bool(begin_value)
        for source, destination in numeric_map.items():
            value = _parse_integer(legacy.get(source, ""))
            if value is not None:
                result[destination] = value
        if "last" in legacy:
            result["fault"] = LEGACY_FAULT_NAMES.get(legacy["last"], legacy["last"])
        return result

    return result


def _unknown_nodes() -> list[dict[str, Any]]:
    return [
        {
            "node_id": node_id,
            "rpm": 0,
            "status": 0x80,
            "applied_duty": FULL_SPEED,
            "age_100ms": 255,
            "missed_polls": 0,
            "uplink_rssi_dbm": fanlink_cli.RSSI_UNKNOWN,
            "downlink_rssi_dbm": fanlink_cli.RSSI_UNKNOWN,
        }
        for node_id in range(1, NODE_COUNT + 1)
    ]


def _unknown_link_metrics() -> list[dict[str, int]]:
    return [
        {
            "uplink_rssi_dbm": fanlink_cli.RSSI_UNKNOWN,
            "downlink_rssi_dbm": fanlink_cli.RSSI_UNKNOWN,
        }
        for _ in range(NODE_COUNT)
    ]


def _default_serial_opener(port: str, baud: int):
    return fanlink_cli.open_serial(port, baud)


def _default_port_lister() -> Iterable[object]:
    try:
        from serial.tools import list_ports  # type: ignore
    except ImportError as exc:  # pragma: no cover - dependency is deployment-specific.
        raise PanelError(
            "pyserial is required to enumerate serial ports",
            status=503,
            code="serial_unavailable",
        ) from exc
    return list_ports.comports()


def _normalise_port(item: object) -> dict[str, Any]:
    if isinstance(item, str):
        return {"device": item, "description": item, "hwid": ""}
    if isinstance(item, Mapping):
        device = item.get("device")
        description = item.get("description", device)
        hwid = item.get("hwid", "")
        extra_source = item
    else:
        device = getattr(item, "device", None)
        description = getattr(item, "description", device)
        hwid = getattr(item, "hwid", "")
        extra_source = None
    if not isinstance(device, str) or not device:
        raise PanelError("serial port provider returned an invalid device", status=500)

    result: dict[str, Any] = {
        "device": device,
        "description": str(description or device),
        "hwid": str(hwid or ""),
    }
    for name in ("vid", "pid", "serial_number", "manufacturer", "product", "location"):
        value = extra_source.get(name) if extra_source is not None else getattr(item, name, None)
        if value is not None and isinstance(value, (str, int)) and not isinstance(value, bool):
            result[name] = value
    return result


class FanLinkPanel:
    """Thread-safe serial/demo state owned by the HTTP handlers."""

    def __init__(
        self,
        *,
        demo: bool = False,
        serial_opener: Optional[Callable[[str, int], object]] = None,
        port_lister: Optional[Callable[[], Iterable[object]]] = None,
        send_interval: float = SEND_INTERVAL_SECONDS,
        monotonic: Callable[[], float] = time.monotonic,
        wall_time: Callable[[], float] = time.time,
        auto_start_demo: bool = True,
    ) -> None:
        if not 0.0 < send_interval < 0.5:
            raise ValueError("send_interval must be positive and below the TX watchdog")
        self._demo = bool(demo)
        self._serial_opener = serial_opener or _default_serial_opener
        self._port_lister = port_lister or _default_port_lister
        self._send_interval = send_interval
        self._monotonic = monotonic
        self._wall_time = wall_time
        self._lock = threading.RLock()
        self._parser = fanlink_cli.StreamParser()
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._serial: Optional[object] = None
        self._token = 0
        self._connecting = False
        self._connected = False
        self._port: Optional[str] = None
        self._duties = [FULL_SPEED] * NODE_COUNT
        self._host_failsafe = True
        self._next_poll_id = 1
        self._uptime_seconds = 0
        self._nodes = _unknown_nodes()
        self._node_online = [False] * NODE_COUNT
        self._link_metrics = _unknown_link_metrics()
        self._send_sequence = 0
        self._last_send_at: Optional[float] = None
        self._last_status_at: Optional[float] = None
        self._last_status_monotonic: Optional[float] = None
        self._last_link_metrics_at: Optional[float] = None
        self._last_link_metrics_monotonic: Optional[float] = None
        self._last_error: Optional[str] = None
        self._serial_rejected_frames = 0
        self._unknown_frames = 0
        self._status_decode_errors = 0
        self._link_metrics_decode_errors = 0
        self._diagnostics: deque[dict[str, Any]] = deque(maxlen=32)
        self._demo_started = self._monotonic()
        if self._demo and auto_start_demo:
            self.connect("demo")

    def _reset_session_locked(self) -> None:
        self._duties = [FULL_SPEED] * NODE_COUNT
        self._host_failsafe = True
        self._next_poll_id = 1
        self._uptime_seconds = 0
        self._nodes = _unknown_nodes()
        self._node_online = [False] * NODE_COUNT
        self._link_metrics = _unknown_link_metrics()
        self._send_sequence = 0
        self._last_send_at = None
        self._last_status_at = None
        self._last_status_monotonic = None
        self._last_link_metrics_at = None
        self._last_link_metrics_monotonic = None
        self._last_error = None
        self._parser = fanlink_cli.StreamParser()
        self._serial_rejected_frames = 0
        self._unknown_frames = 0
        self._status_decode_errors = 0
        self._link_metrics_decode_errors = 0
        self._diagnostics.clear()
        self._demo_started = self._monotonic()

    def list_ports(self) -> list[dict[str, Any]]:
        if self._demo:
            return [
                {
                    "device": "demo",
                    "description": "FanLink 8-node demo",
                    "hwid": "FANLINK-DEMO",
                }
            ]
        try:
            ports = [_normalise_port(item) for item in self._port_lister()]
        except PanelError:
            raise
        except Exception as exc:
            raise PanelError(
                f"cannot enumerate serial ports: {exc}",
                status=503,
                code="serial_unavailable",
            ) from exc
        return sorted(ports, key=lambda item: item["device"])

    def connect(self, port: object) -> dict[str, Any]:
        port_name = validate_port_name(port)
        available = {item["device"] for item in self.list_ports()}
        if port_name not in available:
            raise PanelError("port is not present in the current serial-port list")

        with self._lock:
            if self._connected or self._connecting:
                raise PanelError("a FanLink connection is already active", status=409, code="conflict")
            self._connecting = True
            self._token += 1
            token = self._token
            self._reset_session_locked()

        serial_port: Optional[object] = None
        try:
            if not self._demo:
                serial_port = self._serial_opener(port_name, SERIAL_BAUD)
        except Exception as exc:
            with self._lock:
                if token == self._token:
                    self._connecting = False
                    self._last_error = str(exc)
            raise PanelError(
                f"cannot open serial port {port_name}: {exc}",
                status=502,
                code="connect_failed",
            ) from exc

        stop_event = threading.Event()
        target = self._run_demo if self._demo else self._run_serial
        thread = threading.Thread(
            target=target,
            args=(token, stop_event, serial_port),
            name="fanlink-panel-demo" if self._demo else "fanlink-panel-serial",
            daemon=True,
        )
        with self._lock:
            if token != self._token:
                if serial_port is not None:
                    try:
                        serial_port.close()  # type: ignore[attr-defined]
                    except Exception:
                        pass
                raise PanelError("connection was cancelled", status=409, code="conflict")
            self._stop_event = stop_event
            self._thread = thread
            self._serial = serial_port
            self._port = port_name
            self._connected = True
            self._connecting = False
            try:
                # Publish only a started thread.  Otherwise a concurrent
                # disconnect could try to join a Thread that has not started.
                thread.start()
            except Exception as exc:
                self._thread = None
                self._serial = None
                self._port = None
                self._connected = False
                self._duties = [FULL_SPEED] * NODE_COUNT
                self._host_failsafe = True
                self._last_error = str(exc)
                if serial_port is not None:
                    try:
                        serial_port.close()  # type: ignore[attr-defined]
                    except Exception:
                        pass
                raise PanelError(
                    f"cannot start serial worker: {exc}",
                    status=503,
                    code="connect_failed",
                ) from exc
        return self.state()

    def disconnect(self) -> dict[str, Any]:
        with self._lock:
            self._token += 1
            stop_event = self._stop_event
            thread = self._thread
            serial_port = self._serial
            self._connecting = False
            self._connected = False
            self._port = None
            self._thread = None
            self._serial = None
            self._duties = [FULL_SPEED] * NODE_COUNT
            self._host_failsafe = True
            self._next_poll_id = 1
            self._uptime_seconds = 0
            self._nodes = _unknown_nodes()
            self._link_metrics = _unknown_link_metrics()
            self._last_send_at = None
            self._last_status_at = None
            self._last_status_monotonic = None
            self._last_link_metrics_at = None
            self._last_link_metrics_monotonic = None
        stop_event.set()
        if serial_port is not None:
            try:
                serial_port.close()  # type: ignore[attr-defined]
            except Exception:
                pass
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=1.5)
        return self.state()

    close = disconnect

    def _require_connected_locked(self) -> None:
        if not self._connected:
            raise PanelError("connect to a FanLink transmitter first", status=409, code="not_connected")

    def set_node_duty(self, node: object, duty: object) -> dict[str, Any]:
        node_id = validate_node(node)
        duty_value = validate_duty(duty)
        with self._lock:
            self._require_connected_locked()
            self._duties[node_id - 1] = duty_value
        return self.state()

    def set_all_duties(self, duties: object) -> dict[str, Any]:
        duty_values = validate_duties(duties)
        with self._lock:
            self._require_connected_locked()
            self._duties = duty_values
        return self.state()

    def set_uniform_duty(self, duty: object) -> dict[str, Any]:
        duty_value = validate_duty(duty)
        with self._lock:
            self._require_connected_locked()
            self._duties = [duty_value] * NODE_COUNT
        return self.state()

    def full_speed(self) -> dict[str, Any]:
        with self._lock:
            self._require_connected_locked()
            self._duties = [FULL_SPEED] * NODE_COUNT
        return self.state()

    def _finish_worker(self, token: int, error: Optional[BaseException]) -> None:
        with self._lock:
            if token != self._token:
                return
            self._connected = False
            self._connecting = False
            self._port = None
            self._serial = None
            self._thread = None
            self._duties = [FULL_SPEED] * NODE_COUNT
            self._host_failsafe = True
            self._next_poll_id = 1
            self._uptime_seconds = 0
            self._nodes = _unknown_nodes()
            self._link_metrics = _unknown_link_metrics()
            self._last_send_at = None
            self._last_status_at = None
            self._last_status_monotonic = None
            self._last_link_metrics_at = None
            self._last_link_metrics_monotonic = None
            if error is not None:
                self._last_error = str(error)

    def _run_serial(
        self, token: int, stop_event: threading.Event, serial_port: Optional[object]
    ) -> None:
        if serial_port is None:
            self._finish_worker(token, RuntimeError("serial port was not opened"))
            return
        next_send = self._monotonic()
        failure: Optional[BaseException] = None
        try:
            while not stop_event.is_set():
                now = self._monotonic()
                if now >= next_send:
                    with self._lock:
                        if token != self._token or not self._connected:
                            return
                        payload = bytes(self._duties)
                        sequence = self._send_sequence
                    next_sequence = fanlink_cli.send_frame(
                        serial_port, fanlink_cli.SET_ALL_DUTY, sequence, payload
                    )
                    with self._lock:
                        if token == self._token:
                            self._send_sequence = next_sequence
                            self._last_send_at = self._wall_time()
                    next_send = self._monotonic() + self._send_interval

                waiting_value = getattr(serial_port, "in_waiting", 0)
                try:
                    waiting = int(waiting_value or 0)
                except (TypeError, ValueError):
                    waiting = 0
                read_size = max(1, min(waiting, 4096))
                chunk = serial_port.read(read_size)  # type: ignore[attr-defined]
                if chunk:
                    self._ingest_worker_bytes(token, bytes(chunk))
        except Exception as exc:
            if not stop_event.is_set():
                failure = exc
        finally:
            try:
                serial_port.close()  # type: ignore[attr-defined]
            except Exception:
                pass
            self._finish_worker(token, failure)

    def ingest_serial_bytes(self, chunk: bytes) -> None:
        self._ingest_serial_bytes(chunk, worker_token=None)

    def _ingest_worker_bytes(self, token: int, chunk: bytes) -> None:
        self._ingest_serial_bytes(chunk, worker_token=token)

    def _ingest_serial_bytes(self, chunk: bytes, *, worker_token: Optional[int]) -> None:
        if not isinstance(chunk, bytes):
            raise TypeError("serial input must be bytes")
        with self._lock:
            if worker_token is not None and (
                worker_token != self._token or not self._connected
            ):
                return
            frames = list(self._parser.feed(chunk))
            self._serial_rejected_frames = self._parser.rejected_frames
            # Keep parsing and applying a batch atomic with respect to
            # disconnect.  A late read from an old worker must never restore
            # telemetry after the connection has been cleared.
            for frame in frames:
                self._handle_frame(frame)

    def _handle_frame(self, frame: fanlink_cli.Frame) -> None:
        if frame.message_type == fanlink_cli.STATUS_SNAPSHOT:
            try:
                snapshot = fanlink_cli.decode_status_snapshot(frame.payload)
            except (fanlink_cli.FanLinkError, ValueError, TypeError):
                with self._lock:
                    self._status_decode_errors += 1
                return
            nodes = [
                {
                    "node_id": node.node_id,
                    "rpm": node.rpm,
                    "status": node.status,
                    "applied_duty": node.applied_duty,
                    "age_100ms": node.age_100ms,
                    "missed_polls": node.missed_polls,
                }
                for node in snapshot.nodes
            ]
            with self._lock:
                # Expire the previous evidence before accepting a new snapshot.
                # Merely resuming Host STATUS must not revive an offline RX.
                self._update_node_presence_locked(self._monotonic())
                self._host_failsafe = snapshot.host_failsafe
                self._next_poll_id = snapshot.next_poll_id
                self._uptime_seconds = snapshot.uptime_seconds
                self._nodes = nodes
                self._last_status_at = self._wall_time()
                self._last_status_monotonic = self._monotonic()
                self._update_node_presence_locked(self._last_status_monotonic, allow_recovery=True)
            return

        if frame.message_type == fanlink_cli.ERROR_EVENT:
            raw = frame.payload.decode("utf-8", errors="replace")
            diagnostic = parse_radio_diagnostic(raw)
            diagnostic["frame_sequence"] = frame.sequence
            diagnostic["received_at"] = self._wall_time()
            with self._lock:
                self._diagnostics.append(diagnostic)
            return

        if frame.message_type == fanlink_cli.LINK_METRICS_SNAPSHOT:
            try:
                snapshot = fanlink_cli.decode_link_metrics_snapshot(frame.payload)
            except (fanlink_cli.FanLinkError, ValueError, TypeError):
                with self._lock:
                    self._link_metrics_decode_errors += 1
                return
            metrics = [
                {
                    "uplink_rssi_dbm": node.uplink_rssi_dbm,
                    "downlink_rssi_dbm": node.downlink_rssi_dbm,
                }
                for node in snapshot.nodes
            ]
            with self._lock:
                self._link_metrics = metrics
                self._last_link_metrics_at = self._wall_time()
                self._last_link_metrics_monotonic = self._monotonic()
            return

        with self._lock:
            self._unknown_frames += 1

    def _run_demo(
        self, token: int, stop_event: threading.Event, _serial_port: Optional[object]
    ) -> None:
        while not stop_event.is_set():
            with self._lock:
                if token != self._token or not self._connected:
                    return
                elapsed = max(0.0, self._monotonic() - self._demo_started)
                duties = list(self._duties)
                sequence = int(elapsed * 100.0) & 0xFFFF
                next_poll_id = sequence % NODE_COUNT + 1
                stale_window = int(elapsed) % 15 in (12, 13)
                nodes: list[dict[str, Any]] = []
                metrics: list[dict[str, int]] = []
                for index, duty in enumerate(duties):
                    node_id = index + 1
                    transient_stale = node_id == 7 and stale_window
                    tach_timeout = node_id == 7 and int(elapsed * 2.0) % 24 == 23
                    if duty < 8 or tach_timeout:
                        rpm = 0
                    else:
                        baseline = 420.0 + duty * 8.2 + node_id * 17.0
                        ripple = math.sin(elapsed * 2.1 + node_id * 0.63) * 45.0
                        rpm = max(0, min(65535, int(baseline + ripple)))
                    status = 0
                    if tach_timeout:
                        status |= 0x02
                    if transient_stale:
                        status |= 0x80
                    uplink_rssi = (
                        fanlink_cli.RSSI_UNKNOWN
                        if transient_stale
                        else max(-96, min(-34, int(-43 - node_id * 1.3 + ripple / 30.0)))
                    )
                    downlink_rssi = (
                        fanlink_cli.RSSI_UNKNOWN
                        if transient_stale
                        else max(-99, min(-36, int(-48 - node_id * 1.1 - ripple / 36.0)))
                    )
                    nodes.append(
                        {
                            "node_id": node_id,
                            "rpm": rpm,
                            "status": status,
                            "applied_duty": duty,
                            "age_100ms": 255 if transient_stale else (sequence + node_id) % 3,
                            "missed_polls": 2 if transient_stale else 0,
                        }
                    )
                    metrics.append(
                        {
                            "uplink_rssi_dbm": uplink_rssi,
                            "downlink_rssi_dbm": downlink_rssi,
                        }
                    )
                timeout_count = int(elapsed) // 15
                fault = 4 if stale_window else 0
                last_rx_error = -6 if stale_window else 0
                raw = (
                    f"RF b1 B0 f{fault} e{last_rx_error} X0 R{last_rx_error} "
                    f"n{next_poll_id} s{sequence} x0 t{timeout_count} c0 i0 v0 "
                    f"o{int(elapsed * 6.25)}"
                )
                diagnostic = parse_radio_diagnostic(raw)
                diagnostic["frame_sequence"] = sequence
                diagnostic["received_at"] = self._wall_time()
                if not self._diagnostics or self._diagnostics[-1]["raw"] != raw:
                    self._diagnostics.append(diagnostic)
                self._host_failsafe = False
                self._next_poll_id = next_poll_id
                self._uptime_seconds = int(elapsed)
                self._update_node_presence_locked(self._monotonic())
                self._nodes = nodes
                self._link_metrics = metrics
                self._last_status_at = self._wall_time()
                self._last_status_monotonic = self._monotonic()
                self._update_node_presence_locked(self._last_status_monotonic, allow_recovery=True)
                self._last_link_metrics_at = self._wall_time()
                self._last_link_metrics_monotonic = self._monotonic()
            stop_event.wait(0.2)
        self._finish_worker(token, None)

    def _update_node_presence_locked(self, now: float, *, allow_recovery: bool = False) -> None:
        snapshot_age = (
            None if self._last_status_monotonic is None
            else max(0.0, now - self._last_status_monotonic)
        )
        trusted = (self._connected and snapshot_age is not None
                   and snapshot_age < STATUS_STALE_SECONDS)
        for index, node in enumerate(self._nodes):
            age = node["age_100ms"] * 0.1 + (snapshot_age or 0.0)
            eligible = (trusted and node["age_100ms"] != 255
                        and node["missed_polls"] < NODE_OFFLINE_MISSED_POLLS
                        and age < NODE_OFFLINE_SECONDS)
            if not eligible:
                self._node_online[index] = False
            elif allow_recovery and node["missed_polls"] == 0 and not (node["status"] & 0x80) and age < NODE_TELEMETRY_STALE_SECONDS:
                # TX clears misses only after validating a new RX Reply.
                self._node_online[index] = True

    def state(self) -> dict[str, Any]:
        with self._lock:
            self._update_node_presence_locked(self._monotonic())
            duties = list(self._duties)
            nodes = copy.deepcopy(self._nodes)
            status_age_seconds = (
                None
                if self._last_status_monotonic is None
                else max(0.0, self._monotonic() - self._last_status_monotonic)
            )
            status_snapshot_fresh = (
                self._connected and status_age_seconds is not None
                and status_age_seconds < STATUS_STALE_SECONDS
            )
            link_metrics_age_seconds = (
                None
                if self._last_link_metrics_monotonic is None
                else max(0.0, self._monotonic() - self._last_link_metrics_monotonic)
            )
            link_metrics_snapshot_fresh = (
                self._connected and link_metrics_age_seconds is not None
                and link_metrics_age_seconds < LINK_METRICS_STALE_SECONDS
            )
            link_metrics = (
                copy.deepcopy(self._link_metrics)
                if link_metrics_snapshot_fresh
                else _unknown_link_metrics()
            )
            for node, requested_duty, metrics, online in zip(nodes, duties, link_metrics, self._node_online):
                node["requested_duty"] = requested_duty
                node["effective_age_seconds"] = (
                    None if node["age_100ms"] == 255 or status_age_seconds is None
                    else node["age_100ms"] * 0.1 + status_age_seconds
                )
                node_stale = (
                    not status_snapshot_fresh
                    or bool(node["status"] & 0x80)
                    or node["age_100ms"] == 255
                    or node["age_100ms"] * 0.1 + (status_age_seconds or 0.0) >= NODE_TELEMETRY_STALE_SECONDS
                )
                node["stale"] = node_stale
                node["online"] = online
                node["uplink_rssi_dbm"] = (
                    fanlink_cli.RSSI_UNKNOWN
                    if node_stale
                    else metrics["uplink_rssi_dbm"]
                )
                node["downlink_rssi_dbm"] = (
                    fanlink_cli.RSSI_UNKNOWN
                    if node_stale
                    else metrics["downlink_rssi_dbm"]
                )
            diagnostics = copy.deepcopy(list(self._diagnostics))
            return {
                "demo": self._demo,
                "connected": self._connected,
                "connecting": self._connecting,
                "port": self._port,
                "baud": SERIAL_BAUD,
                "send_interval_seconds": self._send_interval,
                "duties": duties,
                "host_failsafe": self._host_failsafe,
                "next_poll_id": self._next_poll_id,
                "uptime_seconds": self._uptime_seconds,
                "nodes": nodes,
                "diagnostic": diagnostics[-1] if diagnostics else None,
                "diagnostics": diagnostics,
                "send_sequence": self._send_sequence,
                "last_send_at": self._last_send_at,
                "last_status_at": self._last_status_at,
                "last_status_age_seconds": status_age_seconds,
                "status_snapshot_fresh": status_snapshot_fresh,
                "last_link_metrics_at": self._last_link_metrics_at,
                "last_link_metrics_age_seconds": link_metrics_age_seconds,
                "link_metrics_snapshot_fresh": link_metrics_snapshot_fresh,
                "last_error": self._last_error,
                "serial_rejected_frames": self._serial_rejected_frames,
                "unknown_frames": self._unknown_frames,
                "status_decode_errors": self._status_decode_errors,
                "link_metrics_decode_errors": self._link_metrics_decode_errors,
            }


def _require_keys(payload: Mapping[str, Any], allowed: set[str], required: set[str]) -> None:
    keys = set(payload)
    missing = required - keys
    extra = keys - allowed
    if missing:
        raise PanelError(f"missing JSON field: {sorted(missing)[0]}")
    if extra:
        raise PanelError(f"unexpected JSON field: {sorted(extra)[0]}")


class FanLinkHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(
        self,
        server_address: tuple[str, int],
        panel: FanLinkPanel,
        static_directory: Path,
    ) -> None:
        self.panel = panel
        self.static_directory = static_directory.resolve()
        self.loopback_only = _is_loopback_host(server_address[0])
        super().__init__(server_address, FanLinkRequestHandler)


class FanLinkRequestHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "FanLinkPanel/1"

    @property
    def panel_server(self) -> FanLinkHTTPServer:
        return self.server  # type: ignore[return-value]

    def log_message(self, _format: str, *args: object) -> None:
        del args

    def _send_bytes(self, status: int, body: bytes, content_type: str, *, api: bool) -> None:
        self.close_connection = True
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Cache-Control", "no-store" if api else "no-cache")
        self.send_header("Connection", "close")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'self'; script-src 'self'; style-src 'self'; "
            "connect-src 'self'; img-src 'self' data:; object-src 'none'; "
            "base-uri 'none'; frame-ancestors 'none'",
        )
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, status: int, value: object) -> None:
        body = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._send_bytes(status, body, "application/json; charset=utf-8", api=True)

    def _send_error(self, error: PanelError) -> None:
        self._send_json(
            error.status,
            {"ok": False, "error": {"code": error.code, "message": str(error)}},
        )

    def _validate_request_host(self) -> None:
        if not self.panel_server.loopback_only:
            return
        raw_host = self.headers.get("Host", "")
        try:
            hostname = urlsplit(f"//{raw_host}").hostname
        except ValueError as exc:
            raise PanelError("invalid Host header", status=400) from exc
        if not hostname or not _is_loopback_host(hostname):
            raise PanelError("loopback server rejected Host header", status=421, code="bad_host")

    def _read_json_object(self) -> dict[str, Any]:
        if self.headers.get("Transfer-Encoding"):
            raise PanelError("chunked request bodies are not supported")
        raw_length = self.headers.get("Content-Length", "0")
        try:
            length = int(raw_length, 10)
        except ValueError as exc:
            raise PanelError("invalid Content-Length") from exc
        if not 0 <= length <= MAX_REQUEST_BYTES:
            raise PanelError("request body is too large", status=413, code="too_large")
        content_type = self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower()
        if content_type != "application/json":
            raise PanelError(
                "Content-Type must be application/json",
                status=415,
                code="unsupported_media_type",
            )
        if length == 0:
            return {}
        raw = self.rfile.read(length)
        if len(raw) != length:
            raise PanelError("request body ended early")

        def reject_constant(value: str) -> None:
            raise ValueError(f"non-finite number {value} is not valid JSON")

        def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
            result: dict[str, Any] = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError(f"duplicate JSON field {key}")
                result[key] = value
            return result

        try:
            decoded = json.loads(
                raw.decode("utf-8"),
                parse_constant=reject_constant,
                object_pairs_hook=reject_duplicates,
            )
        except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
            raise PanelError(f"invalid JSON: {exc}") from exc
        if not isinstance(decoded, dict):
            raise PanelError("JSON body must be an object")
        return decoded

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API.
        path = urlsplit(self.path).path
        try:
            self._validate_request_host()
            if path == "/api/ports":
                self._send_json(200, {"ports": self.panel_server.panel.list_ports()})
                return
            if path == "/api/state":
                self._send_json(200, self.panel_server.panel.state())
                return
            if path.startswith("/api/"):
                raise PanelError("API endpoint not found", status=404, code="not_found")
            static_entry = STATIC_FILES.get(path)
            if static_entry is None:
                raise PanelError("resource not found", status=404, code="not_found")
            filename, content_type = static_entry
            resource = (self.panel_server.static_directory / filename).resolve()
            if resource.parent != self.panel_server.static_directory or not resource.is_file():
                raise PanelError("resource not found", status=404, code="not_found")
            self._send_bytes(200, resource.read_bytes(), content_type, api=False)
        except PanelError as exc:
            self._send_error(exc)

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API.
        path = urlsplit(self.path).path
        try:
            self._validate_request_host()
            payload = self._read_json_object()
            panel = self.panel_server.panel
            if path == "/api/connect":
                _require_keys(payload, {"port"}, {"port"})
                state = panel.connect(payload["port"])
            elif path == "/api/disconnect":
                _require_keys(payload, set(), set())
                state = panel.disconnect()
            elif path == "/api/duty/node":
                _require_keys(payload, {"node", "duty"}, {"node", "duty"})
                state = panel.set_node_duty(payload["node"], payload["duty"])
            elif path == "/api/duty/all":
                if set(payload) == {"duty"}:
                    state = panel.set_uniform_duty(payload["duty"])
                elif set(payload) == {"duties"}:
                    state = panel.set_all_duties(payload["duties"])
                else:
                    raise PanelError("provide exactly one of duty or duties")
            elif path == "/api/full":
                _require_keys(payload, set(), set())
                state = panel.full_speed()
            else:
                raise PanelError("API endpoint not found", status=404, code="not_found")
            self._send_json(200, {"ok": True, "state": state})
        except PanelError as exc:
            self._send_error(exc)
        except Exception:
            self._send_error(PanelError("internal server error", status=500, code="internal_error"))


def create_server(
    panel: FanLinkPanel,
    *,
    host: str = "127.0.0.1",
    port: int = HTTP_PORT,
    static_directory: Optional[Path] = None,
) -> FanLinkHTTPServer:
    if type(port) is not int or not 0 <= port <= 65535:
        raise ValueError("HTTP port must be 0..65535")
    directory = static_directory or Path(__file__).resolve().parents[1] / "panel"
    return FanLinkHTTPServer((host, port), panel, directory)


def _is_loopback_host(host: str) -> bool:
    if host.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local FanLink TX control panel")
    parser.add_argument("--host", default="127.0.0.1", help="HTTP listen address")
    parser.add_argument("--port", type=int, default=HTTP_PORT, help="HTTP listen port")
    parser.add_argument("--demo", action="store_true", help="simulate a dynamic 8-node network")
    parser.add_argument("--open", action="store_true", help="open the panel in the default browser")
    parser.add_argument(
        "--allow-remote",
        action="store_true",
        help="required when explicitly binding the HTTP server beyond loopback",
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    if not 0 <= args.port <= 65535:
        parser.error("--port must be 0..65535")
    if not _is_loopback_host(args.host) and not args.allow_remote:
        parser.error("non-loopback --host requires explicit --allow-remote")

    panel = FanLinkPanel(demo=args.demo)
    try:
        server = create_server(panel, host=args.host, port=args.port)
    except OSError as exc:
        panel.close()
        print(f"error: cannot start HTTP server: {exc}")
        return 2

    actual_host, actual_port = server.server_address[:2]
    browser_host = "127.0.0.1" if actual_host in ("0.0.0.0", "::") else actual_host
    if ":" in browser_host and not browser_host.startswith("["):
        browser_host = f"[{browser_host}]"
    url = f"http://{browser_host}:{actual_port}/"
    print(f"FanLink panel listening at {url}")
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()
        server.server_close()
        panel.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
