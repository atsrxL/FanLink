#!/usr/bin/env python3
"""Read-only FanLink tach counters. No mode changes, reboot or counter reset.

Requires upgraded TX and RX; unsupported targets time out (not zero counters).
Use an exclusive UART or an explicitly enabled C6 socket://HOST:5000 passthrough.
Opening UART can have driver-dependent electrical effects; disconnect reset wires.
This tool does not enable passthrough or send control/heartbeat; an exclusive
maintenance connection can therefore cause the existing host watchdog fullspeed.
"""
from __future__ import annotations
import argparse
from dataclasses import asdict, dataclass
import json
import math
import secrets
import struct
import time
from fanlink_cli import FanLinkError, StreamParser, crc16_ccitt_false, encode_frame

HOST_REQUEST, HOST_REPLY = 0x16, 0x94

def encode_request(network: int, session: int, node: int) -> bytes:
    if not 0 <= network <= 0xffffffff or not 1 <= session <= 0xffffffff or not 1 <= node <= 8:
        raise FanLinkError("invalid tach network/session/node")
    body = b"\xf1\xa5\x01\x42" + struct.pack("<IIB", network, session, node) + bytes(15)
    return body + struct.pack("<H", crc16_ccitt_false(body))

@dataclass(frozen=True)
class Reply:
    network: int
    session: int
    node: int
    enabled: bool
    raw_edges: int
    valid_periods: int
    filter_rejects: int
    last_edge_age_ms: int | None

def decode_reply(data: bytes) -> Reply:
    if (len(data) != 30 or data[:4] != b"\xf1\xa5\x01\x43" or
            struct.unpack_from("<H", data, 28)[0] != crc16_ccitt_false(data[:28])):
        raise FanLinkError("invalid tach reply envelope/CRC")
    network, session, node, flags, raw, valid, rejects, age = struct.unpack_from("<IIBBIIIH", data, 4)
    if not session or not 1 <= node <= 8 or flags & ~1:
        raise FanLinkError("invalid tach reply fields")
    return Reply(network, session, node, bool(flags), raw, valid, rejects, None if age == 65535 else age)

class Client:
    def __init__(self, port, network: int, *, timeout: float = 1.0, clock=time.monotonic):
        if timeout <= 0: raise ValueError("timeout must be positive")
        self.port, self.network, self.timeout, self.clock = port, network, timeout, clock
        self.parser = StreamParser()
        self.sequence = secrets.randbelow(65536)
        self.session = secrets.randbelow(0xffffffff) + 1

    def query(self, node: int) -> Reply:
        self.session = self.session % 0xffffffff + 1
        session, sequence = self.session, self.sequence
        self.sequence = (sequence + 1) & 65535
        packet = encode_frame(HOST_REQUEST, sequence, encode_request(self.network, session, node))
        if self.port.write(packet) != len(packet): raise FanLinkError("short tach request write")
        self.port.flush()
        deadline = self.clock() + self.timeout
        while self.clock() < deadline:
            waiting = getattr(self.port, "in_waiting", 0)
            for frame in self.parser.feed(self.port.read(min(waiting, 4096) if waiting else 1)):
                if frame.message_type != HOST_REPLY or frame.sequence != sequence: continue
                try: reply = decode_reply(frame.payload)
                except FanLinkError: continue
                if (reply.network, reply.session, reply.node) == (self.network, session, node): return reply
        raise FanLinkError("no matched tach reply: unsupported/busy/lost response, not zero counters")

def open_port(port: str, baud: int):
    try: import serial
    except ImportError as exc: raise FanLinkError("pyserial required: uv run --with pyserial pc/tools/fanlink_tach.py ...") from exc
    if "://" in port and not port.startswith("socket://"):
        raise FanLinkError("only local serial or socket:// passthrough is supported")
    if port.startswith("socket://"):
        return serial.serial_for_url(port, baudrate=baud, timeout=0.05, write_timeout=1)
    connection = serial.Serial(port=None, baudrate=baud, timeout=0.05, write_timeout=1, exclusive=True)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    connection.open()
    return connection

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--network", required=True, type=lambda s: int(s, 0))
    parser.add_argument("--node", required=True, type=int, choices=range(1, 9))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--interval", type=float, default=1.25)
    args = parser.parse_args()
    if (args.count < 1 or not math.isfinite(args.interval) or args.interval < 1.25 or
            not 0 <= args.network <= 0xffffffff or args.baud <= 0):
        parser.error("network uint32, baud >0, count >=1 and finite interval >=1.25 required")
    try:
        with open_port(args.port, args.baud) as port:
            client = Client(port, args.network)
            for index in range(args.count):
                if index: time.sleep(args.interval)
                print(json.dumps(asdict(client.query(args.node))), flush=True)
        return 0
    except (FanLinkError, OSError) as exc:
        parser.exit(1, f"tach diagnostic: {exc}\n")

if __name__ == "__main__":
    raise SystemExit(main())
