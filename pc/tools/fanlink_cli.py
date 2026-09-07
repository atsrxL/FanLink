#!/usr/bin/env python3
"""Minimal FanLink host utility.

This tool intentionally implements only the project-specific serial framing. It
is suitable for bring-up, protocol inspection and as reference code for the
real host application.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass
from typing import Iterable, Iterator, Optional, Sequence

MAGIC = b"FL"
VERSION = 1
MAX_PAYLOAD = 128

SET_ALL_DUTY = 0x01
SET_ONE_DUTY = 0x02
HEARTBEAT = 0x03
STATUS_SNAPSHOT = 0x80
ERROR_EVENT = 0x81
LINK_METRICS_SNAPSHOT = 0x82

LINK_METRICS_PAYLOAD_SIZE = 32
RSSI_UNKNOWN = -127


class FanLinkError(RuntimeError):
    pass


def crc16_ccitt_false(data: bytes, initial: int = 0xFFFF) -> int:
    crc = initial
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(message_type: int, sequence: int, payload: bytes = b"") -> bytes:
    if not 0 <= message_type <= 0xFF:
        raise ValueError("message_type must be 0..255")
    if not 0 <= sequence <= 0xFFFF:
        raise ValueError("sequence must be 0..65535")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload exceeds {MAX_PAYLOAD} bytes")
    header = MAGIC + struct.pack("<BBHH", VERSION, message_type, len(payload), sequence)
    body = header + payload
    return body + struct.pack("<H", crc16_ccitt_false(body))


@dataclass(frozen=True)
class Frame:
    message_type: int
    sequence: int
    payload: bytes


class StreamParser:
    def __init__(self) -> None:
        self._buffer = bytearray()
        self.rejected_frames = 0

    def feed(self, chunk: bytes) -> Iterator[Frame]:
        self._buffer.extend(chunk)
        while True:
            magic_index = self._buffer.find(MAGIC)
            if magic_index < 0:
                # Preserve a trailing 'F' because it may be the start of MAGIC.
                self._buffer[:] = b"F" if self._buffer.endswith(b"F") else b""
                return
            if magic_index:
                del self._buffer[:magic_index]
            if len(self._buffer) < 8:
                return

            version, message_type, payload_length, sequence = struct.unpack_from(
                "<BBHH", self._buffer, 2
            )
            if payload_length > MAX_PAYLOAD:
                self.rejected_frames += 1
                del self._buffer[0]
                continue

            total_length = 8 + payload_length + 2
            if len(self._buffer) < total_length:
                return

            candidate = bytes(self._buffer[:total_length])
            expected_crc = struct.unpack_from("<H", candidate, total_length - 2)[0]
            actual_crc = crc16_ccitt_false(candidate[:-2])
            if version != VERSION or expected_crc != actual_crc:
                self.rejected_frames += 1
                del self._buffer[0]
                continue

            payload = candidate[8:-2]
            del self._buffer[:total_length]
            yield Frame(message_type, sequence, payload)


@dataclass(frozen=True)
class NodeStatus:
    node_id: int
    rpm: int
    status: int
    applied_duty: int
    age_100ms: int
    missed_polls: int


@dataclass(frozen=True)
class StatusSnapshot:
    host_failsafe: bool
    next_poll_id: int
    uptime_seconds: int
    nodes: tuple[NodeStatus, ...]


@dataclass(frozen=True)
class NodeLinkMetrics:
    node_id: int
    uplink_rssi_dbm: int
    downlink_rssi_dbm: int


@dataclass(frozen=True)
class LinkMetricsSnapshot:
    nodes: tuple[NodeLinkMetrics, ...]


def decode_status_snapshot(payload: bytes) -> StatusSnapshot:
    if len(payload) != 102:
        raise FanLinkError(f"STATUS_SNAPSHOT length is {len(payload)}, expected 102")
    flags = payload[0]
    next_poll_id = payload[1]
    uptime_seconds = struct.unpack_from("<I", payload, 2)[0]
    nodes = []
    for index in range(16):
        offset = 6 + index * 6
        rpm = struct.unpack_from("<H", payload, offset)[0]
        nodes.append(
            NodeStatus(
                node_id=index + 1,
                rpm=rpm,
                status=payload[offset + 2],
                applied_duty=payload[offset + 3],
                age_100ms=payload[offset + 4],
                missed_polls=payload[offset + 5],
            )
        )
    return StatusSnapshot(bool(flags & 0x01), next_poll_id, uptime_seconds, tuple(nodes))


def decode_link_metrics_snapshot(payload: bytes) -> LinkMetricsSnapshot:
    if len(payload) != LINK_METRICS_PAYLOAD_SIZE:
        raise FanLinkError(
            f"LINK_METRICS_SNAPSHOT length is {len(payload)}, "
            f"expected {LINK_METRICS_PAYLOAD_SIZE}"
        )
    nodes = []
    for index in range(16):
        uplink_rssi_dbm, downlink_rssi_dbm = struct.unpack_from("<bb", payload, index * 2)
        nodes.append(
            NodeLinkMetrics(
                node_id=index + 1,
                uplink_rssi_dbm=uplink_rssi_dbm,
                downlink_rssi_dbm=downlink_rssi_dbm,
            )
        )
    return LinkMetricsSnapshot(tuple(nodes))


def print_snapshot(snapshot: StatusSnapshot) -> None:
    state = "FAILSAFE" if snapshot.host_failsafe else "ACTIVE"
    print(
        f"TX={state} uptime={snapshot.uptime_seconds}s next_poll={snapshot.next_poll_id}",
        flush=True,
    )
    for node in snapshot.nodes:
        age = "stale" if node.age_100ms == 255 else f"{node.age_100ms / 10:.1f}s"
        print(
            f"  N{node.node_id:02d} rpm={node.rpm:5d} duty={node.applied_duty:3d} "
            f"status=0x{node.status:02X} age={age:>5} miss={node.missed_polls:3d}",
            flush=True,
        )


def print_link_metrics(snapshot: LinkMetricsSnapshot) -> None:
    def display(value: int) -> str:
        return "unknown" if value == RSSI_UNKNOWN else f"{value} dBm"

    print("Link metrics:", flush=True)
    for node in snapshot.nodes:
        print(
            f"  N{node.node_id:02d} uplink={display(node.uplink_rssi_dbm):>11} "
            f"downlink={display(node.downlink_rssi_dbm):>11}",
            flush=True,
        )


def check_byte(value: int, name: str) -> int:
    if not 0 <= value <= 255:
        raise FanLinkError(f"{name} must be 0..255, got {value}")
    return value


def check_node(value: int) -> int:
    if not 1 <= value <= 16:
        raise FanLinkError(f"node must be 1..16, got {value}")
    return value


def open_serial(port_name: str, baud: int):
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise FanLinkError(
            "pyserial is required for hardware access; run: "
            "python3 -m pip install 'pyserial>=3.5,<4'"
        ) from exc
    try:
        return serial.Serial(port_name, baudrate=baud, timeout=0.05, write_timeout=1.0)
    except Exception as exc:  # pyserial exposes platform-specific exception types.
        raise FanLinkError(f"cannot open serial port {port_name!r}: {exc}") from exc


def send_frame(serial_port, message_type: int, sequence: int, payload: bytes = b"") -> int:
    frame = encode_frame(message_type, sequence, payload)
    written = serial_port.write(frame)
    serial_port.flush()
    if written != len(frame):
        raise FanLinkError(f"short serial write: {written}/{len(frame)}")
    return (sequence + 1) & 0xFFFF


def consume_serial(serial_port, parser: StreamParser, *, print_unknown: bool = False) -> None:
    waiting = getattr(serial_port, "in_waiting", 0)
    chunk = serial_port.read(waiting if waiting else 1)
    if not chunk:
        return
    for frame in parser.feed(chunk):
        if frame.message_type == STATUS_SNAPSHOT:
            print_snapshot(decode_status_snapshot(frame.payload))
        elif frame.message_type == ERROR_EVENT:
            print("TX error:", frame.payload.decode("utf-8", errors="replace"), file=sys.stderr)
        elif frame.message_type == LINK_METRICS_SNAPSHOT:
            print_link_metrics(decode_link_metrics_snapshot(frame.payload))
        elif print_unknown:
            print(
                f"unknown frame type=0x{frame.message_type:02X} "
                f"seq={frame.sequence} len={len(frame.payload)}"
            )


def command_payload(args: argparse.Namespace) -> tuple[int, bytes]:
    if args.command == "set":
        return SET_ONE_DUTY, bytes((check_node(args.node), check_byte(args.duty, "duty")))
    if args.command == "set-list":
        if len(args.values) != 16:
            raise FanLinkError("set-list requires exactly 16 values")
        return SET_ALL_DUTY, bytes(check_byte(v, "duty") for v in args.values)
    if args.command == "full":
        return SET_ALL_DUTY, bytes([255] * 16)
    if args.command == "heartbeat":
        return HEARTBEAT, b""
    raise FanLinkError(f"command {args.command!r} has no one-shot payload")


def run_one_shot(args: argparse.Namespace) -> None:
    with open_serial(args.port, args.baud) as serial_port:
        message_type, payload = command_payload(args)
        send_frame(serial_port, message_type, 0, payload)


def run_monitor(args: argparse.Namespace) -> None:
    parser = StreamParser()
    with open_serial(args.port, args.baud) as serial_port:
        while True:
            consume_serial(serial_port, parser, print_unknown=args.verbose)


def hold_values(args: argparse.Namespace) -> bytes:
    if args.all_value is not None:
        value = check_byte(args.all_value, "--all")
        return bytes([value] * 16)
    if args.values is None or len(args.values) != 16:
        raise FanLinkError("hold requires --all VALUE or exactly 16 values after --values")
    return bytes(check_byte(v, "duty") for v in args.values)


def run_hold(args: argparse.Namespace) -> None:
    payload = hold_values(args)
    parser = StreamParser()
    sequence = 0
    interval = args.interval
    if interval <= 0 or interval >= 0.5:
        raise FanLinkError("--interval must be >0 and <0.5 seconds for the default 500 ms watchdog")
    started = time.monotonic()
    next_send = started

    with open_serial(args.port, args.baud) as serial_port:
        while True:
            now = time.monotonic()
            if now >= next_send:
                sequence = send_frame(serial_port, SET_ALL_DUTY, sequence, payload)
                next_send = now + interval
            consume_serial(serial_port, parser, print_unknown=args.verbose)
            if args.duration > 0 and now - started >= args.duration:
                return


def self_test() -> None:
    def require(condition: bool, message: str) -> None:
        if not condition:
            raise FanLinkError(f"self-test failed: {message}")

    require(crc16_ccitt_false(b"123456789") == 0x29B1, "CRC vector")
    payload = bytes(range(16))
    encoded = encode_frame(SET_ALL_DUTY, 0x1234, payload)
    parser = StreamParser()
    frames = list(parser.feed(b"noise\x46" + encoded))
    require(len(frames) == 1, "frame count after resync")
    require(frames[0] == Frame(SET_ALL_DUTY, 0x1234, payload), "frame round-trip")

    corrupted = bytearray(encoded)
    corrupted[9] ^= 0x80
    frames = list(parser.feed(bytes(corrupted) + encoded))
    require(len(frames) == 1, "corrupt frame recovery count")
    require(frames[0].payload == payload, "corrupt frame recovery payload")
    require(parser.rejected_frames >= 1, "corrupt frame rejection counter")

    status_payload = bytearray(102)
    status_payload[0] = 1
    status_payload[1] = 7
    struct.pack_into("<I", status_payload, 2, 123)
    struct.pack_into("<HBBBB", status_payload, 6, 1500, 0x04, 128, 2, 3)
    snapshot = decode_status_snapshot(bytes(status_payload))
    require(snapshot.host_failsafe, "status failsafe flag")
    require(snapshot.next_poll_id == 7, "status poll id")
    require(snapshot.uptime_seconds == 123, "status uptime")
    require(snapshot.nodes[0].rpm == 1500, "status RPM")
    require(snapshot.nodes[0].applied_duty == 128, "status duty")

    metrics_payload = bytearray([RSSI_UNKNOWN & 0xFF] * LINK_METRICS_PAYLOAD_SIZE)
    struct.pack_into("<bb", metrics_payload, 0, -48, -63)
    metrics = decode_link_metrics_snapshot(bytes(metrics_payload))
    require(metrics.nodes[0].uplink_rssi_dbm == -48, "uplink RSSI")
    require(metrics.nodes[0].downlink_rssi_dbm == -63, "downlink RSSI")
    require(metrics.nodes[1].uplink_rssi_dbm == RSSI_UNKNOWN, "unknown RSSI sentinel")
    print("PASS: fanlink_cli self-test")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="FanLink serial control and monitor tool")
    parser.add_argument("--port", help="serial port, for example COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--self-test", action="store_true", help="run protocol self-test and exit")
    parser.add_argument("--verbose", action="store_true")

    commands = parser.add_subparsers(dest="command")

    set_cmd = commands.add_parser("set", help="send one node duty once")
    set_cmd.add_argument("node", type=int)
    set_cmd.add_argument("duty", type=int)

    set_list = commands.add_parser("set-list", help="send 16 duty values once")
    set_list.add_argument("values", type=int, nargs=16)

    commands.add_parser("full", help="send all nodes full speed once")
    commands.add_parser("heartbeat", help="send one heartbeat")
    commands.add_parser("monitor", help="monitor status without maintaining watchdog")

    hold = commands.add_parser("hold", help="maintain duty commands and monitor status")
    group = hold.add_mutually_exclusive_group(required=True)
    group.add_argument("--all", dest="all_value", type=int, help="same duty for all nodes")
    group.add_argument("--values", type=int, nargs=16, help="16 node duty values")
    hold.add_argument("--interval", type=float, default=0.2, help="command interval in seconds")
    hold.add_argument("--duration", type=float, default=0.0, help="0 means run until Ctrl-C")

    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not args.command:
        if not args.self_test:
            parser.error("a command is required unless --self-test is used")
    if not args.port:
        if not args.self_test:
            parser.error("--port is required for serial commands")

    try:
        if args.self_test:
            self_test()
            return 0
        if args.command in {"set", "set-list", "full", "heartbeat"}:
            run_one_shot(args)
        elif args.command == "monitor":
            run_monitor(args)
        elif args.command == "hold":
            run_hold(args)
        else:
            raise FanLinkError(f"unsupported command: {args.command}")
        return 0
    except KeyboardInterrupt:
        return 130
    except FanLinkError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
