from __future__ import annotations

import http.client
import json
import struct
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools import fanlink_cli, fanlink_panel  # noqa: E402


class FakeSerial:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._incoming = bytearray()
        self.writes: list[bytes] = []
        self.closed = False

    @property
    def in_waiting(self) -> int:
        with self._lock:
            return len(self._incoming)

    def queue_input(self, data: bytes) -> None:
        with self._lock:
            self._incoming.extend(data)

    def write(self, data: bytes) -> int:
        with self._lock:
            if self.closed:
                raise OSError("serial port is closed")
            self.writes.append(bytes(data))
        return len(data)

    def flush(self) -> None:
        if self.closed:
            raise OSError("serial port is closed")

    def read(self, size: int = 1) -> bytes:
        deadline = time.monotonic() + 0.005
        while time.monotonic() < deadline:
            with self._lock:
                if self.closed:
                    raise OSError("serial port is closed")
                if self._incoming:
                    count = min(max(1, size), len(self._incoming))
                    result = bytes(self._incoming[:count])
                    del self._incoming[:count]
                    return result
            time.sleep(0.001)
        return b""

    def close(self) -> None:
        with self._lock:
            self.closed = True

    def write_count(self) -> int:
        with self._lock:
            return len(self.writes)

    def copy_writes(self) -> list[bytes]:
        with self._lock:
            return list(self.writes)


def wait_until(predicate, timeout: float = 1.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.005)
    raise AssertionError("condition did not become true before timeout")


def status_payload() -> bytes:
    payload = bytearray(102)
    payload[0] = 0
    payload[1] = 9
    struct.pack_into("<I", payload, 2, 321)
    for index in range(16):
        offset = 6 + index * 6
        struct.pack_into(
            "<HBBBB",
            payload,
            offset,
            1000 + index * 100,
            0x80 if index == 12 else 0,
            200 - index,
            255 if index == 12 else index % 3,
            4 if index == 12 else 0,
        )
    return bytes(payload)


def link_metrics_payload() -> bytes:
    payload = bytearray(fanlink_cli.LINK_METRICS_PAYLOAD_SIZE)
    for index in range(16):
        struct.pack_into("<bb", payload, index * 2, -42 - index, -55 - index)
    return bytes(payload)


class ValidationTests(unittest.TestCase):
    def test_duty_and_node_validation_is_strict(self) -> None:
        self.assertEqual(fanlink_panel.validate_duty(0), 0)
        self.assertEqual(fanlink_panel.validate_duty(255), 255)
        self.assertEqual(fanlink_panel.validate_node(1), 1)
        self.assertEqual(fanlink_panel.validate_node(16), 16)
        for invalid in (-1, 256, True, False, 1.5, "1", None):
            with self.subTest(duty=invalid), self.assertRaises(fanlink_panel.PanelError):
                fanlink_panel.validate_duty(invalid)
        for invalid in (0, 17, True, 1.0, "1", None):
            with self.subTest(node=invalid), self.assertRaises(fanlink_panel.PanelError):
                fanlink_panel.validate_node(invalid)

    def test_full_duty_array_requires_exactly_16_bytes(self) -> None:
        self.assertEqual(fanlink_panel.validate_duties(list(range(16))), list(range(16)))
        for invalid in ([1] * 15, [1] * 17, tuple([1] * 16), [1] * 15 + [True]):
            with self.subTest(value=invalid), self.assertRaises(fanlink_panel.PanelError):
                fanlink_panel.validate_duties(invalid)

    def test_port_name_rejects_empty_whitespace_and_controls(self) -> None:
        self.assertEqual(fanlink_panel.validate_port_name("/dev/cu.test"), "/dev/cu.test")
        for invalid in ("", " COM1", "COM1 ", "bad\nport", 3, None):
            with self.subTest(value=invalid), self.assertRaises(fanlink_panel.PanelError):
                fanlink_panel.validate_port_name(invalid)

    def test_diagnostic_parser_supports_compact_and_legacy(self) -> None:
        compact_text = "RF b1 B0 f4 e-6 X-5 R-6 n1 s123 x2 t100 c3 i4 v5 o6"
        compact = fanlink_panel.parse_radio_diagnostic(compact_text)
        self.assertEqual(compact["raw"], compact_text)
        self.assertEqual(compact["format"], "compact")
        self.assertTrue(compact["begin_ok"])
        self.assertEqual(compact["fault"], "receive_timeout")
        self.assertEqual(compact["driver_error"], -6)
        self.assertEqual(compact["last_tx_error"], -5)
        self.assertEqual(compact["last_rx_error"], -6)
        self.assertEqual(compact["transmit_failures"], 2)
        self.assertEqual(compact["receive_timeouts"], 100)
        self.assertEqual(compact["accepted_replies"], 6)

        legacy_text = (
            "RF b=1 be=0 last=rxio e=-705 n=4 s=90 "
            "tx=1 to=2 crc=3 io=4 inv=5 ok=6"
        )
        legacy = fanlink_panel.parse_radio_diagnostic(legacy_text)
        self.assertEqual(legacy["raw"], legacy_text)
        self.assertEqual(legacy["format"], "legacy")
        self.assertEqual(legacy["fault"], "receive_io")
        self.assertEqual(legacy["driver_error"], -705)
        self.assertEqual(legacy["receive_io_errors"], 4)
        self.assertEqual(legacy["invalid_replies"], 5)


class LinkMetricsProtocolTests(unittest.TestCase):
    def test_link_metrics_decoder_is_signed_ordered_and_strict(self) -> None:
        self.assertEqual(fanlink_cli.LINK_METRICS_SNAPSHOT, 0x82)
        payload = bytearray(link_metrics_payload())
        struct.pack_into("<bb", payload, 14, fanlink_cli.RSSI_UNKNOWN, -128)
        snapshot = fanlink_cli.decode_link_metrics_snapshot(bytes(payload))
        self.assertEqual(len(snapshot.nodes), 16)
        self.assertEqual(snapshot.nodes[0].node_id, 1)
        self.assertEqual(snapshot.nodes[0].uplink_rssi_dbm, -42)
        self.assertEqual(snapshot.nodes[0].downlink_rssi_dbm, -55)
        self.assertEqual(snapshot.nodes[7].node_id, 8)
        self.assertEqual(snapshot.nodes[7].uplink_rssi_dbm, fanlink_cli.RSSI_UNKNOWN)
        self.assertEqual(snapshot.nodes[7].downlink_rssi_dbm, -128)

        for invalid in (bytes(payload[:-1]), bytes(payload) + b"\x00"):
            with self.subTest(length=len(invalid)), self.assertRaises(fanlink_cli.FanLinkError):
                fanlink_cli.decode_link_metrics_snapshot(invalid)


class PanelStateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.serial = FakeSerial()
        self.panel = fanlink_panel.FanLinkPanel(
            serial_opener=lambda _port, _baud: self.serial,
            port_lister=lambda: [
                {"device": "/dev/cu.fake", "description": "Fake TX", "hwid": "FAKE"}
            ],
            send_interval=0.02,
        )

    def tearDown(self) -> None:
        self.panel.close()

    def test_safe_defaults_and_unlisted_port_rejection(self) -> None:
        state = self.panel.state()
        self.assertFalse(state["connected"])
        self.assertTrue(state["host_failsafe"])
        self.assertEqual(state["duties"], [255] * 16)
        self.assertTrue(all(node["requested_duty"] == 255 for node in state["nodes"]))
        self.assertTrue(
            all(node["uplink_rssi_dbm"] == fanlink_cli.RSSI_UNKNOWN for node in state["nodes"])
        )
        self.assertTrue(
            all(node["downlink_rssi_dbm"] == fanlink_cli.RSSI_UNKNOWN for node in state["nodes"])
        )
        self.assertIsNone(state["last_link_metrics_at"])
        self.assertFalse(state["link_metrics_snapshot_fresh"])
        with self.assertRaises(fanlink_panel.PanelError):
            self.panel.connect("/tmp/not-a-serial-port")

    def test_connect_sends_full_state_then_stops_after_disconnect(self) -> None:
        state = self.panel.connect("/dev/cu.fake")
        self.assertTrue(state["connected"])
        wait_until(lambda: self.serial.write_count() >= 1)

        parser = fanlink_cli.StreamParser()
        frames = []
        for write in self.serial.copy_writes():
            frames.extend(parser.feed(write))
        self.assertGreaterEqual(len(frames), 1)
        self.assertEqual(frames[0].message_type, fanlink_cli.SET_ALL_DUTY)
        self.assertEqual(frames[0].payload, bytes([255] * 16))

        self.panel.set_node_duty(3, 77)
        wait_until(lambda: self.serial.write_count() >= 2)
        parser = fanlink_cli.StreamParser()
        frames = []
        for write in self.serial.copy_writes():
            frames.extend(parser.feed(write))
        self.assertTrue(any(frame.payload[2] == 77 for frame in frames[1:]))

        self.panel.ingest_serial_bytes(
            fanlink_cli.encode_frame(
                fanlink_cli.LINK_METRICS_SNAPSHOT, 9, link_metrics_payload()
            )
        )
        self.assertEqual(
            self.panel.state()["nodes"][0]["uplink_rssi_dbm"],
            fanlink_cli.RSSI_UNKNOWN,
        )
        self.panel.ingest_serial_bytes(
            fanlink_cli.encode_frame(
                fanlink_cli.STATUS_SNAPSHOT, 10, status_payload()
            )
        )
        self.assertEqual(self.panel.state()["nodes"][0]["uplink_rssi_dbm"], -42)

        disconnected = self.panel.disconnect()
        self.assertFalse(disconnected["connected"])
        self.assertEqual(disconnected["duties"], [255] * 16)
        self.assertIsNone(disconnected["last_status_at"])
        self.assertIsNone(disconnected["last_link_metrics_at"])
        self.assertFalse(disconnected["link_metrics_snapshot_fresh"])
        self.assertTrue(
            all(
                node["uplink_rssi_dbm"] == fanlink_cli.RSSI_UNKNOWN
                and node["downlink_rssi_dbm"] == fanlink_cli.RSSI_UNKNOWN
                for node in disconnected["nodes"]
            )
        )
        self.assertTrue(all(node["stale"] for node in disconnected["nodes"]))
        write_count = self.serial.write_count()
        time.sleep(0.06)
        self.assertEqual(self.serial.write_count(), write_count)

    def test_status_and_error_frames_update_json_state(self) -> None:
        self.panel.ingest_serial_bytes(
            fanlink_cli.encode_frame(fanlink_cli.STATUS_SNAPSHOT, 10, status_payload())
        )
        self.panel.ingest_serial_bytes(
            fanlink_cli.encode_frame(
                fanlink_cli.LINK_METRICS_SNAPSHOT, 12, link_metrics_payload()
            )
        )
        # A later legacy status frame must not erase the independently reported RSSI.
        self.panel.ingest_serial_bytes(
            fanlink_cli.encode_frame(fanlink_cli.STATUS_SNAPSHOT, 13, status_payload())
        )
        compact = "RF b1 B0 f4 e-6 X0 R-6 n13 s55 x0 t7 c0 i0 v0 o8"
        self.panel.ingest_serial_bytes(
            fanlink_cli.encode_frame(fanlink_cli.ERROR_EVENT, 11, compact.encode("ascii"))
        )
        state = self.panel.state()
        self.assertFalse(state["host_failsafe"])
        self.assertEqual(state["next_poll_id"], 9)
        self.assertEqual(state["uptime_seconds"], 321)
        self.assertEqual(len(state["nodes"]), 16)
        self.assertEqual(state["nodes"][0]["rpm"], 1000)
        self.assertEqual(state["nodes"][0]["uplink_rssi_dbm"], -42)
        self.assertEqual(state["nodes"][0]["downlink_rssi_dbm"], -55)
        self.assertTrue(state["link_metrics_snapshot_fresh"])
        self.assertIsNotNone(state["last_link_metrics_at"])
        self.assertTrue(state["nodes"][12]["stale"])
        self.assertEqual(state["diagnostic"]["raw"], compact)
        self.assertEqual(state["diagnostic"]["last_rx_error"], -6)
        self.assertEqual(state["diagnostic"]["frame_sequence"], 11)

    def test_late_bytes_from_disconnected_worker_are_ignored(self) -> None:
        self.panel.connect("/dev/cu.fake")
        stale_token = self.panel._token
        self.panel.disconnect()
        frame = (
            fanlink_cli.encode_frame(fanlink_cli.STATUS_SNAPSHOT, 10, status_payload())
            + fanlink_cli.encode_frame(
                fanlink_cli.LINK_METRICS_SNAPSHOT, 11, link_metrics_payload()
            )
        )
        self.panel._ingest_worker_bytes(stale_token, frame)
        state = self.panel.state()
        self.assertIsNone(state["last_status_at"])
        self.assertFalse(state["status_snapshot_fresh"])
        self.assertFalse(state["link_metrics_snapshot_fresh"])
        self.assertTrue(
            all(node["uplink_rssi_dbm"] == fanlink_cli.RSSI_UNKNOWN for node in state["nodes"])
        )
        self.assertTrue(all(node["stale"] for node in state["nodes"]))

    def test_bad_or_stale_link_metrics_are_exposed_as_unknown(self) -> None:
        monotonic_now = [100.0]
        panel = fanlink_panel.FanLinkPanel(
            monotonic=lambda: monotonic_now[0],
            auto_start_demo=False,
        )
        try:
            panel.ingest_serial_bytes(
                fanlink_cli.encode_frame(
                    fanlink_cli.STATUS_SNAPSHOT, 1, status_payload()
                )
            )
            panel.ingest_serial_bytes(
                fanlink_cli.encode_frame(
                    fanlink_cli.LINK_METRICS_SNAPSHOT, 1, link_metrics_payload()
                )
            )
            fresh = panel.state()
            self.assertTrue(fresh["link_metrics_snapshot_fresh"])
            self.assertEqual(fresh["nodes"][15]["uplink_rssi_dbm"], -57)
            self.assertEqual(fresh["nodes"][15]["downlink_rssi_dbm"], -70)
            self.assertTrue(fresh["nodes"][12]["stale"])
            self.assertEqual(
                fresh["nodes"][12]["uplink_rssi_dbm"], fanlink_cli.RSSI_UNKNOWN
            )

            monotonic_now[0] += fanlink_panel.LINK_METRICS_STALE_SECONDS + 0.01
            stale = panel.state()
            self.assertFalse(stale["link_metrics_snapshot_fresh"])
            self.assertTrue(
                all(
                    node["uplink_rssi_dbm"] == fanlink_cli.RSSI_UNKNOWN
                    and node["downlink_rssi_dbm"] == fanlink_cli.RSSI_UNKNOWN
                    for node in stale["nodes"]
                )
            )

            panel.ingest_serial_bytes(
                fanlink_cli.encode_frame(
                    fanlink_cli.LINK_METRICS_SNAPSHOT, 2, link_metrics_payload()[:-1]
                )
            )
            self.assertEqual(panel.state()["link_metrics_decode_errors"], 1)
        finally:
            panel.close()

    def test_expired_status_snapshot_marks_every_node_stale(self) -> None:
        monotonic_now = [100.0]
        panel = fanlink_panel.FanLinkPanel(
            monotonic=lambda: monotonic_now[0],
            auto_start_demo=False,
        )
        try:
            panel.ingest_serial_bytes(
                fanlink_cli.encode_frame(
                    fanlink_cli.STATUS_SNAPSHOT, 10, status_payload()
                )
            )
            fresh = panel.state()
            self.assertTrue(fresh["status_snapshot_fresh"])
            self.assertFalse(fresh["nodes"][0]["stale"])

            monotonic_now[0] += fanlink_panel.STATUS_STALE_SECONDS + 0.01
            expired = panel.state()
            self.assertFalse(expired["status_snapshot_fresh"])
            self.assertTrue(all(node["stale"] for node in expired["nodes"]))
        finally:
            panel.close()

    def test_duty_updates_require_a_connection(self) -> None:
        with self.assertRaises(fanlink_panel.PanelError) as caught:
            self.panel.set_uniform_duty(100)
        self.assertEqual(caught.exception.status, 409)


class DemoTests(unittest.TestCase):
    def test_demo_has_dynamic_16_node_state_and_reconnects_safely(self) -> None:
        panel = fanlink_panel.FanLinkPanel(demo=True, send_interval=0.02)
        try:
            wait_until(lambda: panel.state()["last_status_at"] is not None)
            state = panel.state()
            self.assertTrue(state["demo"])
            self.assertTrue(state["connected"])
            self.assertEqual(len(state["nodes"]), 16)
            self.assertTrue(any(node["rpm"] > 0 for node in state["nodes"]))
            self.assertTrue(
                any(node["uplink_rssi_dbm"] != fanlink_cli.RSSI_UNKNOWN for node in state["nodes"])
            )
            self.assertTrue(
                any(node["downlink_rssi_dbm"] != fanlink_cli.RSSI_UNKNOWN for node in state["nodes"])
            )
            self.assertTrue(state["link_metrics_snapshot_fresh"])
            self.assertIsNotNone(state["diagnostic"])
            panel.set_all_duties(list(range(16)))
            self.assertEqual(panel.state()["duties"], list(range(16)))
            panel.disconnect()
            self.assertEqual(panel.state()["duties"], [255] * 16)
            panel.connect("demo")
            self.assertEqual(panel.state()["duties"], [255] * 16)
        finally:
            panel.close()


class ApiTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        static = Path(self.tempdir.name)
        (static / "index.html").write_text("<h1>FanLink</h1>", encoding="utf-8")
        (static / "app.css").write_text("body{}", encoding="utf-8")
        (static / "app.js").write_text("console.log('ok')", encoding="utf-8")
        self.panel = fanlink_panel.FanLinkPanel(demo=True, send_interval=0.02)
        self.server = fanlink_panel.create_server(
            self.panel, host="127.0.0.1", port=0, static_directory=static
        )
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.port = self.server.server_address[1]

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=1.0)
        self.panel.close()
        self.tempdir.cleanup()

    def request(
        self,
        method: str,
        path: str,
        body: object = None,
        *,
        raw_body: bytes | None = None,
        content_type: str = "application/json",
    ) -> tuple[int, dict | str]:
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2.0)
        headers = {}
        encoded: bytes | None
        if raw_body is not None:
            encoded = raw_body
        elif body is not None:
            encoded = json.dumps(body).encode("utf-8")
        else:
            encoded = None
        if encoded is not None:
            headers["Content-Type"] = content_type
        connection.request(method, path, body=encoded, headers=headers)
        response = connection.getresponse()
        response_body = response.read()
        response_type = response.getheader("Content-Type", "")
        status = response.status
        connection.close()
        if response_type.startswith("application/json"):
            return status, json.loads(response_body)
        return status, response_body.decode("utf-8")

    def test_get_ports_state_and_static_whitelist(self) -> None:
        status, ports = self.request("GET", "/api/ports")
        self.assertEqual(status, 200)
        self.assertEqual(ports["ports"][0]["device"], "demo")
        status, state = self.request("GET", "/api/state")
        self.assertEqual(status, 200)
        self.assertTrue(state["connected"])
        self.assertIn("uplink_rssi_dbm", state["nodes"][0])
        self.assertIn("downlink_rssi_dbm", state["nodes"][0])

        status, page = self.request("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn("FanLink", page)
        status, css = self.request("GET", "/app.css")
        self.assertEqual((status, css), (200, "body{}"))
        status, result = self.request("GET", "/../README.md")
        self.assertEqual(status, 404)
        self.assertFalse(result["ok"])

    def test_duty_full_disconnect_and_connect_endpoints(self) -> None:
        status, result = self.request("POST", "/api/duty/node", {"node": 2, "duty": 44})
        self.assertEqual(status, 200)
        self.assertEqual(result["state"]["duties"][1], 44)

        values = list(range(16))
        status, result = self.request("POST", "/api/duty/all", {"duties": values})
        self.assertEqual(status, 200)
        self.assertEqual(result["state"]["duties"], values)
        status, result = self.request("POST", "/api/duty/all", {"duty": 88})
        self.assertEqual(status, 200)
        self.assertEqual(result["state"]["duties"], [88] * 16)

        status, result = self.request("POST", "/api/full", {})
        self.assertEqual(status, 200)
        self.assertEqual(result["state"]["duties"], [255] * 16)
        status, result = self.request("POST", "/api/disconnect", {})
        self.assertEqual(status, 200)
        self.assertFalse(result["state"]["connected"])
        self.assertEqual(result["state"]["duties"], [255] * 16)
        status, result = self.request("POST", "/api/connect", {"port": "demo"})
        self.assertEqual(status, 200)
        self.assertTrue(result["state"]["connected"])

    def test_api_rejects_bad_types_extra_fields_and_non_json(self) -> None:
        status, result = self.request(
            "POST", "/api/duty/node", {"node": True, "duty": 20}
        )
        self.assertEqual(status, 400)
        self.assertFalse(result["ok"])
        status, _ = self.request(
            "POST", "/api/duty/node", {"node": 1, "duty": 20, "extra": 1}
        )
        self.assertEqual(status, 400)
        status, _ = self.request(
            "POST", "/api/full", raw_body=b"{}", content_type="text/plain"
        )
        self.assertEqual(status, 415)
        status, _ = self.request(
            "POST", "/api/full", raw_body=b'{"x":1,"x":2}'
        )
        self.assertEqual(status, 400)
        status, _ = self.request("POST", "/api/full", raw_body=b'{"x":NaN}')
        self.assertEqual(status, 400)

    def test_empty_post_cannot_bypass_json_content_type(self) -> None:
        status, result = self.request("POST", "/api/disconnect")
        self.assertEqual(status, 415)
        self.assertFalse(result["ok"])
        self.assertTrue(self.panel.state()["connected"])

    def test_loopback_host_and_clickjacking_headers(self) -> None:
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2.0)
        connection.request("GET", "/")
        response = connection.getresponse()
        response.read()
        self.assertEqual(response.getheader("X-Frame-Options"), "DENY")
        self.assertEqual(
            response.getheader("Cross-Origin-Resource-Policy"), "same-origin"
        )
        self.assertIn("frame-ancestors 'none'", response.getheader("Content-Security-Policy"))
        connection.close()

        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2.0)
        connection.putrequest("GET", "/api/state", skip_host=True)
        connection.putheader("Host", "attacker.example")
        connection.endheaders()
        response = connection.getresponse()
        body = json.loads(response.read())
        self.assertEqual(response.status, 421)
        self.assertEqual(body["error"]["code"], "bad_host")
        connection.close()

    def test_default_cli_binding_is_loopback(self) -> None:
        args = fanlink_panel.build_argument_parser().parse_args([])
        self.assertEqual(args.host, "127.0.0.1")
        self.assertTrue(fanlink_panel._is_loopback_host(args.host))
        self.assertFalse(fanlink_panel._is_loopback_host("0.0.0.0"))


class FrontendContractTests(unittest.TestCase):
    def test_rssi_columns_and_stale_guard_are_present(self) -> None:
        html = (PROJECT_ROOT / "panel" / "index.html").read_text(encoding="utf-8")
        javascript = (PROJECT_ROOT / "panel" / "app.js").read_text(encoding="utf-8")
        self.assertIn('class="uplink-rssi-value rssi-value metric-mono"', html)
        self.assertIn('class="downlink-rssi-value rssi-value metric-mono"', html)
        self.assertIn("RX → TX · dBm", html)
        self.assertIn("TX → RX · dBm", html)
        self.assertIn("link_metrics_snapshot_fresh", javascript)
        self.assertIn("connected && linkMetricsFresh", javascript)
        self.assertIn("numeric === RSSI_UNKNOWN", javascript)


if __name__ == "__main__":
    unittest.main()
