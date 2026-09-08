#!/usr/bin/env python3
"""Query or persist FanLink settings through an exclusive TX UART.

Acknowledgements confirm storage/scheduling only, not reboot, physical pins or
radiated power. No BOOT commands or DTR/RTS operations are performed by this tool.
Network/session filtering is not authentication.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import secrets
import struct
import sys
import time
from typing import Callable, Optional, Sequence

from fanlink_cli import FanLinkError, StreamParser, crc16_ccitt_false, encode_frame

HOST_REQUEST, HOST_REPLY = 0x15, 0x93
QUERY, SET_MODE, SET_POWER = 1, 2, 3
PROFILES = {1: "Lite", 2: "Nano"}
MODES = {"UART": 0, "PWM": 1, "PWMSWAP": 2}
STATUS = ("Ok", "BadRequest", "Unsupported", "Busy", "StorageError", "RadioError")


def allowed_power(profile: int) -> tuple[int, ...]:
    if profile == 1:
        return tuple(range(-18, 14))
    if profile == 2:
        return (-10, -6, -3, 1)
    raise FanLinkError("unknown board profile")


def encode_request(network: int, session: int, node: int, operation: int, value: int = 0) -> bytes:
    if (not 0 <= network <= 0xFFFFFFFF or not 1 <= session <= 0xFFFFFFFF
            or not 0 <= node <= 8 or operation not in (QUERY, SET_MODE, SET_POWER)
            or (operation == QUERY and value != 0)
            or (operation == SET_MODE and (node == 0 or value not in MODES.values()))
            or (operation == SET_POWER and not -18 <= value <= 13)):
        raise FanLinkError("invalid config network/session/node/operation/value")
    body = b"\xf1\xa5\x01\x40" + struct.pack("<IIBBB", network, session, node,
                                                operation, value & 255) + bytes(13)
    return body + struct.pack("<H", crc16_ccitt_false(body))


@dataclass(frozen=True)
class Reply:
    network: int
    session: int
    node: int
    operation: int
    value: int  # raw echoed byte, including signed power encoded in two's complement
    status: int
    profile: int
    mode: int
    power: int
    flags: int
    capabilities: int
    ota_rate: int
    receivers: int
    period_ms: int


def decode_reply(data: bytes) -> Reply:
    if (len(data) != 30 or data[:4] != b"\xf1\xa5\x01\x41"
            or struct.unpack_from("<H", data, 28)[0] != crc16_ccitt_false(data[:28])):
        raise FanLinkError("invalid config response envelope/CRC")
    network, session, node, operation, value = struct.unpack_from("<IIBBB", data, 4)
    status, profile, mode, power, flags, capabilities, rate, receivers, period = struct.unpack_from(
        "<BBBbBIHBB", data, 15)
    if (not session or not 0 <= node <= 8 or operation not in (QUERY, SET_MODE, SET_POWER)
            or status >= len(STATUS) or profile not in PROFILES or power not in allowed_power(profile)
            or mode not in ((255,) if node == 0 else ((0, 1, 2, 254) if flags & 2 else (0, 1, 2))) or flags & ~3
            or capabilities & ~127 or rate != 5120 or receivers != 8 or period != 125):
        raise FanLinkError("invalid config response fields")
    return Reply(network, session, node, operation, value, status, profile, mode,
                 power, flags, capabilities, rate, receivers, period)


class Client:
    def __init__(self, port, network: int, *, attempts: int = 3, timeout: float = 1.2,
                 clock: Callable[[], float] = time.monotonic):
        if not 0 <= network <= 0xFFFFFFFF or attempts < 1 or timeout <= 0:
            raise ValueError("invalid config client settings")
        self.port, self.network = port, network
        self.attempts, self.timeout, self.clock = attempts, timeout, clock
        self.parser = StreamParser()
        self.sequence = secrets.randbelow(65536)
        self.session = secrets.randbelow(0xFFFFFFFF) + 1
        self.mutation_sent = False

    def exchange(self, node: int, operation: int, value: int = 0) -> Reply:
        self.session = self.session % 0xFFFFFFFF + 1
        session = self.session
        sequence = self.sequence
        self.sequence = (sequence + 1) & 65535
        payload = encode_request(self.network, session, node, operation, value)
        request = encode_frame(HOST_REQUEST, sequence, payload)
        for _ in range(self.attempts):
            if operation != QUERY:
                self.mutation_sent = True
            count = self.port.write(request)
            if count != len(request):
                raise FanLinkError(f"short config serial write: {count}/{len(request)}")
            self.port.flush()
            deadline = self.clock() + self.timeout
            while self.clock() < deadline:
                waiting = getattr(self.port, "in_waiting", 0)
                for frame in self.parser.feed(self.port.read(min(waiting, 4096) if waiting else 1)):
                    if frame.message_type != HOST_REPLY or frame.sequence != sequence:
                        continue
                    try:
                        reply = decode_reply(frame.payload)
                    except FanLinkError:
                        continue
                    if (reply.network, reply.session, reply.node, reply.operation, reply.value) != (
                            self.network, session, node, operation, value & 255):
                        continue
                    if reply.status:
                        raise FanLinkError(f"config rejected: {STATUS[reply.status]}")
                    return reply
        raise FanLinkError("config response timeout; no matched acknowledgement")

    def query(self, node: int) -> Reply:
        return self.exchange(node, QUERY)

    def set_value(self, node: int, operation: int, value: int) -> Reply:
        # Discover actual board/capabilities before any mutating request.
        info = self.query(node)
        describe(info)
        required = (1 << 6) | (1 << (4 if operation == SET_MODE else 5))
        if operation not in (SET_MODE, SET_POWER) or info.capabilities & required != required:
            raise FanLinkError("target does not advertise the required persistent setting capability")
        if operation == SET_POWER and value not in allowed_power(info.profile):
            raise FanLinkError(f"unsupported {PROFILES[info.profile]} chip power {value} dBm; "
                               f"allowed={allowed_power(info.profile)}")
        if operation == SET_MODE and (node == 0 or value not in MODES.values()):
            raise FanLinkError("pin modes apply only to RX nodes1..8")
        if info.flags & 1:
            raise FanLinkError("target already has a pending change; query again after apply/reboot")
        result = self.exchange(node, operation, value)
        if result.profile != info.profile or result.capabilities & required != required:
            raise FanLinkError("target profile/capabilities changed during config transaction")
        selected = result.mode if operation == SET_MODE else result.power
        if selected != value:
            raise FanLinkError("acknowledged setting does not match target's selected value")
        return result


def describe(reply: Reply) -> None:
    mode = "TX" if reply.node == 0 else ("SAFE" if reply.mode == 254 else
            next(name for name, value in MODES.items() if value == reply.mode))
    print(f"node={reply.node} profile={PROFILES[reply.profile]} mode={mode} "
          f"chip_power={reply.power} dBm allowed_chip_dBm={allowed_power(reply.profile)} "
          f"capabilities=0x{reply.capabilities:02X} pending={bool(reply.flags & 1)} "
          f"safe_record={bool(reply.flags & 2)} OTA_cap={reply.ota_rate} B/s "
          f"receivers={reply.receivers} poll={reply.period_ms} ms", flush=True)
    if reply.profile == 2:
        print("Nano -10/-6/-3/1 dBm are nominal board 10/25/50/100 mW levels, not measured output.")


def open_serial(port: str, baud: int):
    try:
        import serial
        # No explicit access to DTR/RTS or BOOT. Opening a driver can still have
        # platform-specific electrical effects; do not wire automatic reset here.
        return serial.Serial(port, baudrate=baud, timeout=0.05, write_timeout=1.0, exclusive=True)
    except Exception as exc:
        raise FanLinkError(f"cannot exclusively open config UART {port!r}: {exc}") from exc


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--network", required=True, type=lambda text: int(text, 0))
    parser.add_argument("--baud", default=115200, type=int)
    commands = parser.add_subparsers(dest="command", required=True)
    query = commands.add_parser("query")
    query.add_argument("--node", default=0, choices=range(9), type=int)
    tx_power = commands.add_parser("tx-power")
    tx_power.add_argument("dbm", type=int)
    for name in ("rx-power", "rx-mode"):
        command = commands.add_parser(name)
        command.add_argument("--node", required=True, choices=range(1, 9), type=int)
        if name == "rx-power":
            command.add_argument("dbm", type=int)
        else:
            command.add_argument("mode", type=str.upper, choices=tuple(MODES))
    args = parser.parse_args(argv)
    client = None
    try:
        if not 0 <= args.network <= 0xFFFFFFFF:
            raise FanLinkError("network must be uint32")
        with open_serial(args.port, args.baud) as port:
            client = Client(port, args.network)
            if args.command == "query":
                result = client.query(args.node)
            else:
                node = 0 if args.command == "tx-power" else args.node
                operation = SET_MODE if args.command == "rx-mode" else SET_POWER
                value = MODES[args.mode] if operation == SET_MODE else args.dbm
                result = client.set_value(node, operation, value)
                print("Setting ACK: saved/scheduled only; reboot, pins and RF output NOT verified.")
            describe(result)
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        print(f"Config stopped: {exc or 'interrupted'}", file=sys.stderr)
        if client is not None and client.mutation_sent:
            print("A setting request was sent; its application may be pending. Query after reboot; "
                  "no BOOT/reset or automatic recovery was performed.", file=sys.stderr)
        return 130 if isinstance(exc, KeyboardInterrupt) else 2


if __name__ == "__main__":
    raise SystemExit(main())
