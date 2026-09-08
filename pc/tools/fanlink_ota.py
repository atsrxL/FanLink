#!/usr/bin/env python3
"""Upload RX over RF (default), or explicitly update TX itself over local UART.

--inspect validates the image without opening a serial port. An upload keeps
fans at full speed and commits a reboot only after the receiver verifies MD5.
The protocol filters identities; it does not authenticate firmware or peers.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import secrets
import struct
import sys
import time
from dataclasses import dataclass
from enum import IntEnum
from typing import Callable, Optional, Sequence

from fanlink_cli import FanLinkError, StreamParser, crc16_ccitt_false, encode_frame

IMAGE_MARKER = b"FANLINK-OTA-V1!!"
IMAGE_METADATA = struct.Struct("<IIBBBB")
MAX_IMAGE_SIZE = 512 * 1024
HOST_REQUEST = 0x10
HOST_REPLY = 0x90
HOST_FAST_REQUEST = 0x11
HOST_TX_REQUEST = 0x12
HOST_TX_FAST_REQUEST = 0x13
HOST_TX_REPLY = 0x91
FAST_BLOCK_SIZE = 100
FAST_RATE_LIMIT = 5120  # Image bytes/second; a ceiling, not a throughput guarantee.


class Op(IntEnum):
    BEGIN = 1
    HASH = 2
    DATA = 3
    FINISH = 4
    ABORT = 5
    COMMIT = 6
    FRAGMENT = 7
    BLOCK_FINISH = 8


class State(IntEnum):
    IDLE = 0
    MANIFEST = 1
    RECEIVING = 2
    VERIFIED = 3
    COMMITTED = 4
    FAILED = 5


STATUS_NAMES = ("Ok", "BadState", "BadTarget", "BadOffset", "BadImage", "StorageError", "Busy")


@dataclass(frozen=True)
class FirmwareImage:
    data: bytes
    compatibility: int
    network: int
    node: int
    role: int = 2

    @property
    def md5(self) -> bytes:
        return hashlib.md5(self.data).digest()

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()


def inspect_image(data: bytes, node: int, network: Optional[int] = None, *,
                  target: str = "rx") -> FirmwareImage:
    if target not in ("rx", "tx") or (target == "tx" and node != 1):
        raise FanLinkError("target must be rx/tx; TX requires --node 1")
    if not 1 <= node <= 8:
        raise FanLinkError("node must be 1..8")
    if not 128 <= len(data) <= MAX_IMAGE_SIZE:
        raise FanLinkError(f"raw firmware must be 128..{MAX_IMAGE_SIZE} bytes")
    if data[0] != 0xE9:
        raise FanLinkError("firmware must be an uncompressed ESP8266 raw .bin (magic E9)")
    if data.count(IMAGE_MARKER) != 1:
        raise FanLinkError("firmware must contain exactly one FanLink OTA identity marker")
    offset = data.index(IMAGE_MARKER) + len(IMAGE_MARKER)
    if offset + IMAGE_METADATA.size > len(data):
        raise FanLinkError("truncated OTA image identity")
    compatibility, image_network, role, image_node, version, reserved = IMAGE_METADATA.unpack_from(data, offset)
    expected_role = 1 if target == "tx" else 2
    if role != expected_role or image_node != node or version != 1 or reserved != 0:
        raise FanLinkError(f"OTA identity must be {target.upper()} role={expected_role}, "
                           "matching node, format=1, reserved=0")
    if network is not None and image_network != network:
        raise FanLinkError("firmware network does not match --network")
    return FirmwareImage(data, compatibility, image_network, image_node, role)


def encode_request(image: FirmwareImage, session: int, operation: Op,
                   offset: int = 0, data: bytes = b"") -> bytes:
    if not 1 <= image.node <= 8:
        raise FanLinkError("node must be 1..8")
    if not 1 <= session <= 0xFFFFFFFF or len(data) > 10:
        raise ValueError("nonzero uint32 session and at most ten data bytes required")
    body = b"\xf1\xa5\x01\x30" + struct.pack(
        "<IIBBI", image.network, session, image.node, operation, offset
    ) + data.ljust(10, b"\0")
    return body + struct.pack("<H", crc16_ccitt_false(body))


def encode_fast_block(image: FirmwareImage, session: int, offset: int, data: bytes) -> bytes:
    count = min(FAST_BLOCK_SIZE, len(image.data) - offset)
    if (offset < 0 or offset % FAST_BLOCK_SIZE or count <= 0 or len(data) != count):
        raise ValueError("fast block must start on a 100-byte boundary and contain the exact image block")
    return encode_request(image, session, Op.FRAGMENT, offset, data[:10]) + data[10:]


@dataclass(frozen=True)
class Ack:
    network: int
    session: int
    node: int
    operation: int
    offset: int
    status: int
    state: State
    compatibility: int


def decode_ack(data: bytes) -> Ack:
    if len(data) != 30 or data[:4] != b"\xf1\xa5\x01\x31":
        raise FanLinkError("invalid OTA ACK envelope")
    if crc16_ccitt_false(data[:28]) != struct.unpack_from("<H", data, 28)[0]:
        raise FanLinkError("invalid OTA ACK CRC")
    network, session, node, operation, offset = struct.unpack_from("<IIBBI", data, 4)
    status, state, compatibility = struct.unpack_from("<BBI", data, 18)
    if (not session or not 1 <= node <= 8 or operation not in Op._value2member_map_
            or status >= len(STATUS_NAMES) or state not in State._value2member_map_
            or data[24:28] != b"\0" * 4):
        raise FanLinkError("invalid OTA ACK fields or reserved bytes")
    return Ack(network, session, node, operation, offset, status, State(state), compatibility)


class Uploader:
    """Stop-and-wait transfer. Inject serial and clock for offline fault tests."""

    def __init__(self, serial_port, image: FirmwareImage, *, session: Optional[int] = None,
                 attempts: int = 12, timeout: float = 0.8, begin_timeout: float = 2.0,
                 clock: Callable[[], float] = time.monotonic,
                 sleep: Callable[[float], None] = time.sleep,
                 mode: str = "legacy", rate: int = FAST_RATE_LIMIT,
                 target: str = "rx",
                 progress: Optional[Callable[[int, int], None]] = None) -> None:
        self.serial = serial_port
        self.image = image
        # An explicit target is mandatory for TX; never infer it from an image or ACK.
        checked = inspect_image(image.data, image.node, image.network, target=target)
        if checked != image:
            raise FanLinkError("image metadata does not match inspected binary")
        self.target = target
        self.request_type = HOST_TX_REQUEST if target == "tx" else HOST_REQUEST
        self.fast_request_type = HOST_TX_FAST_REQUEST if target == "tx" else HOST_FAST_REQUEST
        self.reply_type = HOST_TX_REPLY if target == "tx" else HOST_REPLY
        self.session = session if session is not None else secrets.randbelow(0xFFFFFFFF) + 1
        if not 1 <= self.session <= 0xFFFFFFFF or attempts < 1 or min(timeout, begin_timeout) <= 0:
            raise ValueError("invalid session or retry settings")
        self.attempts = attempts
        self.timeout = timeout
        self.begin_timeout = begin_timeout
        self.clock = clock
        if mode not in ("legacy", "fast") or not 1 <= rate <= FAST_RATE_LIMIT:
            raise ValueError("mode must be legacy/fast and rate must be 1..5120 image bytes/second")
        self.mode = mode
        self.rate = rate
        self.sleep = sleep
        self.next_fast_send = 0.0
        self.progress = progress
        self.parser = StreamParser()
        self.sequence = secrets.randbelow(65536)
        self.committed = False
        self.commit_sent = False

    def exchange(self, operation: Op, offset: int, data: bytes,
                 expected_state: State, expected_offset: int, *, attempts: Optional[int] = None,
                 fast_payload: Optional[bytes] = None, fast_count: int = 0) -> Ack:
        sequence = self.sequence
        self.sequence = (self.sequence + 1) & 0xFFFF
        request = encode_frame(self.fast_request_type if fast_payload is not None else self.request_type,
                               sequence, fast_payload if fast_payload is not None else
                               encode_request(self.image, self.session, operation, offset, data))
        for _ in range(self.attempts if attempts is None else attempts):
            if fast_payload is not None:
                now = self.clock()
                if now < self.next_fast_send:
                    self.sleep(self.next_fast_send - now)
                # No accumulated credit/catch-up burst after a stalled exchange.
                # Retransmitted image bytes consume the same pacing budget.
                self.next_fast_send = self.clock() + fast_count / self.rate
            if operation == Op.COMMIT:
                self.commit_sent = True
            written = self.serial.write(request)
            if written != len(request):
                raise FanLinkError(f"short serial write: {written}/{len(request)}")
            self.serial.flush()
            deadline = self.clock() + (self.begin_timeout if operation == Op.BEGIN else self.timeout)
            retry_batch = False
            while self.clock() < deadline:
                waiting = getattr(self.serial, "in_waiting", 0)
                chunk = self.serial.read(min(waiting, 4096) if waiting else 1)
                for frame in self.parser.feed(chunk):
                    if frame.message_type != self.reply_type or frame.sequence != sequence:
                        continue
                    try:
                        ack = decode_ack(frame.payload)
                    except FanLinkError:
                        continue
                    if (ack.network, ack.session, ack.node, ack.operation) != (
                            self.image.network, self.session, self.image.node, operation):
                        continue
                    if ack.status != 0:
                        if (fast_payload is not None and ack.status == 3
                                and ack.compatibility == self.image.compatibility
                                and ack.state == State.RECEIVING
                                and expected_offset - fast_count <= ack.offset <= expected_offset):
                            # Lost RF fragments: RX retains the partial block. Replay
                            # the identical bounded batch, never advance on a NACK.
                            retry_batch = True
                            break
                        raise FanLinkError(f"receiver rejected {operation.name}: {STATUS_NAMES[ack.status]} "
                                           f"(state={ack.state.name}, offset={ack.offset}, "
                                           f"expected compat=0x{self.image.compatibility:08X}, "
                                           f"receiver compat=0x{ack.compatibility:08X})")
                    if (ack.compatibility != self.image.compatibility
                            or ack.state != expected_state or ack.offset != expected_offset):
                        continue
                    return ack
                if retry_batch:
                    break
        raise FanLinkError(f"{operation.name}: no matching receiver ACK after retries")

    def upload(self) -> None:
        size = len(self.image.data)
        try:
            self.exchange(Op.BEGIN, size, struct.pack("<I", self.image.compatibility), State.MANIFEST, 0)
            self.exchange(Op.HASH, 0, self.image.md5[:10], State.MANIFEST, 0)
            self.exchange(Op.HASH, 10, self.image.md5[10:], State.RECEIVING, 0)
            block_size = FAST_BLOCK_SIZE if self.mode == "fast" else 10
            for offset in range(0, size, block_size):
                block = self.image.data[offset:offset + block_size]
                if self.mode == "fast":
                    payload = encode_fast_block(self.image, self.session, offset, block)
                    self.exchange(Op.BLOCK_FINISH, offset + len(block), b"", State.RECEIVING,
                                  offset + len(block), fast_payload=payload, fast_count=len(block))
                else:
                    self.exchange(Op.DATA, offset, block, State.RECEIVING, offset + len(block))
                if self.progress:
                    self.progress(offset + len(block), size)
            self.exchange(Op.FINISH, size, b"", State.VERIFIED, size)
            self.exchange(Op.COMMIT, size, b"", State.COMMITTED, size)
            self.committed = True
        except (Exception, KeyboardInterrupt):
            # Once COMMIT was sent the outcome may be unknown; Abort cannot undo it.
            if not self.commit_sent:
                try:
                    self.exchange(Op.ABORT, 0, b"", State.IDLE, 0, attempts=1)
                except (Exception, KeyboardInterrupt):
                    pass  # Preserve the original error; this is only best effort.
            raise


def open_serial(port: str, baud: int):
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise FanLinkError("pyserial is required for uploads: python3 -m pip install 'pyserial>=3.5,<4'") from exc
    connection = None
    try:
        connection = serial.Serial(port=None, baudrate=baud, timeout=0.05,
                                   write_timeout=1.0, exclusive=True)
        connection.dtr = False
        connection.rts = False
        connection.port = port
        connection.open()
        return connection
    except Exception as exc:
        if connection is not None:
            connection.close()
        raise FanLinkError(f"cannot exclusively open {port!r}: {exc}") from exc


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="TX serial port; release its existing owner before upload")
    parser.add_argument("--firmware", required=True, type=Path, help="target's raw firmware .bin")
    parser.add_argument("--target", choices=("rx", "tx"), default="rx",
                        help="rx: wireless receiver update; tx: local UART self-update (explicit)")
    parser.add_argument("--node", required=True, type=int, choices=range(1, 9))
    parser.add_argument("--network", type=lambda value: int(value, 0), help="optional expected network ID")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--inspect", action="store_true", help="validate image only; no serial access")
    parser.add_argument("--mode", choices=("legacy", "fast"), default="legacy",
                        help="legacy for old RX migration; fast requires fast-capable TX and RX")
    parser.add_argument("--rate", type=int, default=FAST_RATE_LIMIT,
                        help="fast-mode image-byte pacing ceiling, 1..5120 B/s (default 5120)")
    args = parser.parse_args(argv)
    if not args.inspect and not args.port:
        parser.error("--port is required unless --inspect is used")
    if not 1 <= args.rate <= FAST_RATE_LIMIT:
        parser.error("--rate must be 1..5120 image bytes/second")
    if args.target == "tx" and args.node != 1:
        parser.error("TX requires --node 1")
    if args.baud <= 0 or (args.network is not None and not 0 <= args.network <= 0xFFFFFFFF):
        parser.error("baud must be positive and network must be uint32")
    uploader = None
    try:
        if args.firmware.suffix.lower() != ".bin":
            raise FanLinkError("--firmware must be a raw .bin file")
        if args.firmware.stat().st_size > MAX_IMAGE_SIZE:
            raise FanLinkError(f"raw firmware exceeds {MAX_IMAGE_SIZE} bytes")
        image = inspect_image(args.firmware.read_bytes(), args.node, args.network, target=args.target)
        print(f"{args.target.upper()} node={image.node} network=0x{image.network:08X} "
              f"compatibility=0x{image.compatibility:08X} bytes={len(image.data)}")
        print(f"SHA256={image.sha256}")
        if args.inspect:
            print("Image identity valid; receiver compatibility and boot have not been verified.")
            return 0
        print("Starting " + ("TX local UART update" if args.target == "tx" else "RF OTA") +
              "; fans remain at full speed during maintenance.", flush=True)
        print(f"Mode={args.mode}; " + (f"pacing ceiling={args.rate} B/s, actual throughput may be lower."
              if args.mode == "fast" else "legacy 10-byte stop-and-wait."), flush=True)
        last_percent = -1

        def progress(done: int, size: int) -> None:
            nonlocal last_percent
            percent = done * 100 // size
            if percent != last_percent and (percent % 5 == 0 or done == size):
                print(f"Transfer {percent}% ({done}/{size})", flush=True)
                last_percent = percent

        with open_serial(args.port, args.baud) as connection:
            uploader = Uploader(connection, image, progress=progress, mode=args.mode, rate=args.rate,
                                target=args.target)
            uploader.upload()
        print(f"{args.target.upper()} COMMIT ACK received; reboot/version and fan/RF health are NOT yet verified.")
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        print(f"OTA stopped: {exc or 'interrupted'}", file=sys.stderr)
        if uploader is not None and uploader.commit_sent and not uploader.committed:
            print("COMMIT was sent but not confirmed; receiver may already reboot. Verify it before retrying.", file=sys.stderr)
        elif uploader is not None and not uploader.committed:
            print("Uncommitted transfer: best-effort Abort attempted; no successful update claimed.", file=sys.stderr)
        return 130 if isinstance(exc, KeyboardInterrupt) else 2


if __name__ == "__main__":
    raise SystemExit(main())
