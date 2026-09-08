#!/usr/bin/env python3
"""Explicit TX reboot or ROM UART bootloader request; never runs a flasher.

Requires a running TX with local-maintenance support and exclusive UART access.
An accepted ACK is not proof that reboot/ROM entry succeeded. No DTR/RTS pulse,
serial probing, automatic retries or firmware writes are performed.
"""

from __future__ import annotations

import argparse
import math
import secrets
import struct
import sys
import time
from typing import Callable, Optional, Sequence

from fanlink_cli import FanLinkError, StreamParser, encode_frame
from fanlink_ota import open_serial

HOST_CONTROL = 0x14
HOST_CONTROL_ACK = 0x92
GUARD = b"FLTXCTL"
ACTIONS = {"reboot": 1, "boot": 2}
STATUSES = {0: "accepted", 1: "busy", 2: "unsupported"}


def request_control(serial_port, network: int, action: str, *,
                    session: Optional[int] = None, sequence: Optional[int] = None,
                    timeout: float = 2.0,
                    clock: Callable[[], float] = time.monotonic) -> None:
    """Send exactly once. Lost ACK leaves the outcome unknown, never retry blindly."""
    session = secrets.randbelow(0xFFFFFFFF) + 1 if session is None else session
    sequence = secrets.randbelow(65536) if sequence is None else sequence
    if (not 0 <= network <= 0xFFFFFFFF or action not in ACTIONS
            or not 1 <= session <= 0xFFFFFFFF or not 0 <= sequence <= 65535
            or not math.isfinite(timeout) or timeout <= 0):
        raise FanLinkError("invalid network, action, session, sequence or timeout")
    payload = struct.pack("<IIB", network, session, ACTIONS[action]) + GUARD
    request = encode_frame(HOST_CONTROL, sequence, payload)
    written = serial_port.write(request)
    if written != len(request):
        raise FanLinkError(f"short serial write: {written}/{len(request)}; outcome unknown")
    serial_port.flush()
    parser = StreamParser()
    deadline = clock() + timeout
    while clock() < deadline:
        waiting = getattr(serial_port, "in_waiting", 0)
        for frame in parser.feed(serial_port.read(min(waiting, 4096) if waiting else 1)):
            if (frame.message_type != HOST_CONTROL_ACK or frame.sequence != sequence
                    or len(frame.payload) != 17 or frame.payload[:16] != payload):
                continue
            status = frame.payload[16]
            if status not in STATUSES:
                continue
            if status:
                raise FanLinkError(f"TX rejected {action}: {STATUSES[status]}")
            return
    raise FanLinkError("no matching control ACK; outcome unknown, verify TX before retrying")


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=tuple(ACTIONS), help="boot requests ROM UART download mode")
    parser.add_argument("--port", required=True, help="exclusive TX application UART")
    parser.add_argument("--network", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args(argv)
    if not 0 <= args.network <= 0xFFFFFFFF or args.baud <= 0:
        parser.error("network must be uint32 and baud must be positive")
    try:
        print(f"Requesting TX {args.action}; full-speed maintenance precedes the action.", flush=True)
        with open_serial(args.port, args.baud) as connection:
            request_control(connection, args.network, args.action)
        print(f"TX {args.action} request accepted; actual reboot/ROM entry is NOT yet verified.")
        if args.action == "boot":
            print("UART released. No flasher was started and no firmware was written.")
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        print(f"TX control stopped: {exc or 'interrupted; outcome may be unknown'}", file=sys.stderr)
        return 130 if isinstance(exc, KeyboardInterrupt) else 2


if __name__ == "__main__":
    raise SystemExit(main())
