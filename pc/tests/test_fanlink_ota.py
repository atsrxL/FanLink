"""Offline RF OTA host tests; no pyserial or hardware is used."""

from contextlib import redirect_stdout, redirect_stderr
import io
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import fanlink_ota as ota
from fanlink_cli import FanLinkError, StreamParser, crc16_ccitt_false, encode_frame


def raw_image(size=137, *, role=2, node=1, version=1, reserved=0):
    data = bytearray(b"\xe9" + b"\x55" * (size - 1))
    metadata = ota.IMAGE_MARKER + struct.pack("<IIBBBB", 0xAB123456, 0x10203040,
                                            role, node, version, reserved)
    data[32:32 + len(metadata)] = metadata
    return bytes(data)


def reply(request, state, offset, *, status=0):
    data = bytearray(request)
    data[3] = 0x31
    struct.pack_into("<I", data, 14, offset)
    data[18:28] = struct.pack("<BBI", status, state, 0xAB123456) + b"\0" * 4
    struct.pack_into("<H", data, 28, crc16_ccitt_false(data[:28]))
    return bytes(data)


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        self.now += 0.01
        return self.now


class FakeReceiverSerial:
    """Tiny ordered receiver with cached replies and controllable loss/corruption."""

    def __init__(self, *, drop_first=False, silent=False, mutation=None,
                 fail_op=None, status=5, interrupt_op=None, commit_loss=False, target="rx"):
        self.target = target
        self.drop_first = drop_first
        self.silent = silent
        self.mutation = mutation
        self.fail_op = fail_op
        self.status = status
        self.interrupt_op = interrupt_op
        self.commit_loss = commit_loss
        self.buffer = bytearray()
        self.writes = []
        self.cache = {}
        self.image = bytearray()
        self.hash = bytearray()
        self.size = 0
        self.commit_count = 0

    @property
    def in_waiting(self):
        return len(self.buffer)

    def write(self, data):
        frame, = StreamParser().feed(data)
        self.writes.append(frame)
        request = frame.payload
        fast = frame.message_type == (ota.HOST_TX_FAST_REQUEST if self.target == "tx" else ota.HOST_FAST_REQUEST)
        if (not fast and (frame.message_type != (0x12 if self.target == "tx" else 0x10) or len(request) != 30)) or len(request) < 30:
            raise AssertionError("bad serial wrapper")
        if crc16_ccitt_false(request[:28]) != struct.unpack_from("<H", request, 28)[0]:
            raise AssertionError("bad request CRC")
        op = ota.Op(request[13])
        offset = struct.unpack_from("<I", request, 14)[0]
        if self.interrupt_op == op:
            self.interrupt_op = None
            raise KeyboardInterrupt
        duplicate = request in self.cache
        if duplicate:
            ack = self.cache[request]
        elif self.fail_op == op:
            ack_request = bytearray(request[:30])
            if fast:
                ack_request[13] = ota.Op.BLOCK_FINISH
            ack = reply(ack_request, ota.State.FAILED, len(self.image), status=self.status)
        else:
            if op == ota.Op.BEGIN:
                self.size = offset
                if request[22:28] != b"\0" * 6:
                    raise AssertionError("begin padding")
                state = ota.State.MANIFEST
            elif op == ota.Op.HASH:
                if offset != len(self.hash):
                    raise AssertionError("hash ordering")
                self.hash.extend(request[18:28] if offset == 0 else request[18:24])
                if offset == 10 and request[24:28] != b"\0" * 4:
                    raise AssertionError("hash padding")
                state = ota.State.MANIFEST if offset == 0 else ota.State.RECEIVING
            elif op in (ota.Op.DATA, ota.Op.FRAGMENT):
                if offset != len(self.image):
                    raise AssertionError("data ordering or duplicate write")
                count = min(100 if fast else 10, self.size - offset)
                if fast and (op != ota.Op.FRAGMENT or offset % 100 or
                             len(request) != 30 + max(0, count - 10)):
                    raise AssertionError("bad fast block")
                self.image.extend(request[18:18 + min(count, 10)] + (request[30:] if fast else b""))
                if count < 10 and request[18 + count:28] != b"\0" * (10 - count):
                    raise AssertionError("data padding")
                state = ota.State.RECEIVING
            elif op == ota.Op.FINISH:
                if offset != self.size or bytes(self.hash) != ota.hashlib.md5(self.image).digest():
                    raise AssertionError("finish size/digest mismatch")
                state = ota.State.VERIFIED
            elif op == ota.Op.COMMIT:
                self.commit_count += 1
                state = ota.State.COMMITTED
            else:
                self.image.clear()
                state = ota.State.IDLE
            ack_request = bytearray(request[:30])
            if fast:
                ack_request[13] = ota.Op.BLOCK_FINISH
            ack = reply(ack_request, state, len(self.image))
            self.cache[request] = ack
        packet = encode_frame(0x91 if self.target == "tx" else 0x90, frame.sequence, ack)
        if self.mutation:
            packet = self.mutation(frame, ack)
        if not (self.silent or (self.drop_first and not duplicate)
                or (self.commit_loss and op == ota.Op.COMMIT)):
            self.buffer.extend(packet)
        return len(data)

    def read(self, count):
        result = bytes(self.buffer[:count])
        del self.buffer[:count]
        return result

    def flush(self):
        pass


def uploader(serial, **kwargs):
    return ota.Uploader(serial, ota.inspect_image(raw_image(), 1), session=7,
                        attempts=2, timeout=0.05, begin_timeout=0.05, clock=Clock(), **kwargs)


class ImageTests(unittest.TestCase):
    def test_node_eight_allowed_and_nine_rejected_through_request_and_ack(self):
        image = ota.inspect_image(raw_image(node=8), 8)
        request = ota.encode_request(image, 7, ota.Op.BEGIN)
        self.assertEqual((len(request), request[2], request[12]), (30, 1, 8))
        self.assertEqual(ota.decode_ack(reply(request, ota.State.IDLE, 0)).node, 8)
        from dataclasses import replace
        with self.assertRaises(FanLinkError):
            ota.encode_request(replace(image, node=9), 7, ota.Op.BEGIN)
        invalid = bytearray(reply(request, ota.State.IDLE, 0))
        invalid[12] = 9
        struct.pack_into("<H", invalid, 28, crc16_ccitt_false(invalid[:28]))
        with self.assertRaises(FanLinkError):
            ota.decode_ack(bytes(invalid))
        with self.assertRaises(FanLinkError):
            ota.inspect_image(raw_image(node=9), 9)

    def test_identity_and_hash(self):
        image = ota.inspect_image(raw_image(), 1, 0x10203040)
        self.assertEqual(len(ota.IMAGE_MARKER), 16)
        self.assertEqual(image.compatibility, 0xAB123456)
        self.assertEqual(image.md5, ota.hashlib.md5(raw_image()).digest())
        self.assertEqual(len(image.sha256), 64)

    def test_reject_bad_images(self):
        bad = [b"", raw_image()[:127], b"x" + raw_image()[1:],
               b"\xe9" + b"x" * 128, raw_image() + ota.IMAGE_MARKER,
               raw_image(role=1), raw_image(node=16), raw_image(version=2),
               raw_image(reserved=1), raw_image(ota.MAX_IMAGE_SIZE + 1),
               b"\xe9" + b"x" * 130 + ota.IMAGE_MARKER + b"\0"]
        for data in bad:
            with self.subTest(length=len(data), tail=data[-20:]):
                with self.assertRaises(FanLinkError):
                    ota.inspect_image(data, 1)
        for node in (0, 9, 16, 17):
            with self.assertRaises(FanLinkError):
                ota.inspect_image(raw_image(), node)
        with self.assertRaisesRegex(FanLinkError, "network"):
            ota.inspect_image(raw_image(), 1, 99)

    def test_inspect_never_opens_serial(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "firmware.bin"
            path.write_bytes(raw_image())
            with patch.object(ota, "open_serial", side_effect=AssertionError("hardware")), redirect_stdout(io.StringIO()):
                self.assertEqual(ota.main(["--firmware", str(path), "--node", "1", "--inspect"]), 0)


class TransferTests(unittest.TestCase):
    def test_complete_transfer_and_final_partial_block(self):
        serial = FakeReceiverSerial()
        progress = []
        host = uploader(serial, progress=lambda done, size: progress.append((done, size)))
        host.upload()
        self.assertTrue(host.committed)
        self.assertEqual(serial.image, raw_image())
        self.assertEqual(serial.commit_count, 1)
        self.assertEqual(progress[-1], (137, 137))
        operations = [frame.payload[13] for frame in serial.writes]
        self.assertEqual(operations[:3], [1, 2, 2])
        self.assertEqual(operations[-2:], [4, 6])

    def test_lost_acks_retry_identical_frames_without_rewriting(self):
        serial = FakeReceiverSerial(drop_first=True)
        host = uploader(serial)
        host.upload()
        self.assertTrue(host.committed)
        self.assertEqual(serial.image, raw_image())
        self.assertEqual(serial.commit_count, 1)
        for first, second in zip(serial.writes[::2], serial.writes[1::2]):
            self.assertEqual(first, second)

    def test_timeout_is_not_success_and_attempts_abort(self):
        serial = FakeReceiverSerial(silent=True)
        host = uploader(serial)
        with self.assertRaisesRegex(FanLinkError, "BEGIN.*no matching"):
            host.upload()
        self.assertFalse(host.committed)
        self.assertEqual([frame.payload[13] for frame in serial.writes], [1, 1, 5])

    def test_failed_statuses_stop_without_commit(self):
        for status in range(1, 7):
            with self.subTest(status=status):
                serial = FakeReceiverSerial(fail_op=ota.Op.DATA, status=status)
                host = uploader(serial)
                with self.assertRaisesRegex(FanLinkError, ota.STATUS_NAMES[status]):
                    host.upload()
                self.assertFalse(host.committed)
                self.assertEqual(serial.commit_count, 0)
                self.assertEqual(serial.writes[-1].payload[13], ota.Op.ABORT)

    def test_bad_target_reports_receiver_fingerprint_without_timeout(self):
        def mutate(frame, ack):
            changed = bytearray(ack)
            struct.pack_into("<I", changed, 20, 0x87654321)
            struct.pack_into("<H", changed, 28, crc16_ccitt_false(changed[:28]))
            return encode_frame(0x90, frame.sequence, bytes(changed))

        serial = FakeReceiverSerial(fail_op=ota.Op.BEGIN, status=2, mutation=mutate)
        host = uploader(serial)
        with self.assertRaisesRegex(
                FanLinkError, "receiver rejected BEGIN: BadTarget.*expected compat=0xAB123456, "
                "receiver compat=0x87654321"):
            host.upload()
        self.assertFalse(host.committed)
        self.assertEqual(sum(frame.payload[13] == ota.Op.BEGIN for frame in serial.writes), 1)
        self.assertEqual(serial.writes[-1].payload[13], ota.Op.ABORT)

    def test_keyboard_interrupt_aborts_before_commit(self):
        serial = FakeReceiverSerial(interrupt_op=ota.Op.DATA)
        host = uploader(serial)
        with self.assertRaises(KeyboardInterrupt):
            host.upload()
        self.assertFalse(host.committed)
        self.assertEqual(serial.writes[-1].payload[13], ota.Op.ABORT)

    def test_lost_commit_ack_is_uncertain_not_success(self):
        serial = FakeReceiverSerial(commit_loss=True)
        host = uploader(serial)
        with self.assertRaisesRegex(FanLinkError, "COMMIT"):
            host.upload()
        self.assertTrue(host.commit_sent)
        self.assertFalse(host.committed)
        self.assertEqual(serial.commit_count, 1)
        self.assertNotIn(ota.Op.ABORT, [frame.payload[13] for frame in serial.writes])

    def test_wrong_ack_fields_never_advance(self):
        # Each corruption has a fresh valid RF CRC and Host CRC except explicit CRC cases.
        for field_offset in (0, 2, 3, 4, 8, 12, 13, 14, 19, 20, 24, 28):
            def mutate(frame, ack):
                changed = bytearray(ack)
                changed[field_offset] ^= 1
                if field_offset != 28:
                    struct.pack_into("<H", changed, 28, crc16_ccitt_false(changed[:28]))
                return encode_frame(0x90, frame.sequence, bytes(changed))
            with self.subTest(field_offset=field_offset):
                serial = FakeReceiverSerial(mutation=mutate)
                host = uploader(serial)
                with self.assertRaises(FanLinkError):
                    host.upload()
                self.assertEqual(serial.commit_count, 0)
                self.assertFalse(host.committed)

    def test_wrong_serial_type_sequence_and_length(self):
        mutations = [lambda frame, ack: encode_frame(0x80, frame.sequence, ack),
                     lambda frame, ack: encode_frame(0x90, (frame.sequence + 1) & 65535, ack),
                     lambda frame, ack: encode_frame(0x90, frame.sequence, ack[:-1]),
                     lambda frame, ack: encode_frame(0x90, frame.sequence, ack)[:-1] + b"x"]
        for mutation in mutations:
            host = uploader(FakeReceiverSerial(mutation=mutation))
            with self.assertRaises(FanLinkError):
                host.upload()
            self.assertFalse(host.committed)

    def test_unknown_ack_status_and_state_are_rejected(self):
        for offset, value in ((18, 7), (19, 6)):
            def mutate(frame, ack):
                changed = bytearray(ack)
                changed[offset] = value
                struct.pack_into("<H", changed, 28, crc16_ccitt_false(changed[:28]))
                return encode_frame(0x90, frame.sequence, bytes(changed))
            host = uploader(FakeReceiverSerial(mutation=mutate))
            with self.assertRaises(FanLinkError):
                host.upload()
            self.assertFalse(host.committed)

    def test_noise_and_wrong_ack_then_valid_ack(self):
        def mutate(frame, ack):
            return b"noise" + encode_frame(0x80, 1, b"status") + encode_frame(
                0x90, (frame.sequence + 1) & 65535, ack) + encode_frame(0x90, frame.sequence, ack)
        host = uploader(FakeReceiverSerial(mutation=mutate))
        host.upload()
        self.assertTrue(host.committed)

    def test_short_write_aborts_and_does_not_report_success(self):
        serial = FakeReceiverSerial()
        original = serial.write
        serial.write = lambda data: original(data) - 1
        host = uploader(serial)
        with self.assertRaisesRegex(FanLinkError, "short serial write"):
            host.upload()
        self.assertFalse(host.committed)
        self.assertEqual(serial.writes[-1].payload[13], ota.Op.ABORT)

    def test_random_session_is_nonzero_and_rejects_zero(self):
        with patch.object(ota.secrets, "randbelow", return_value=0):
            host = ota.Uploader(FakeReceiverSerial(), ota.inspect_image(raw_image(), 1))
            self.assertEqual(host.session, 1)
        with self.assertRaises(ValueError):
            ota.Uploader(FakeReceiverSerial(), ota.inspect_image(raw_image(), 1), session=0)


class FastTransferTests(unittest.TestCase):
    def test_full_and_partial_fast_blocks(self):
        for size in (128, 137, 200, 201, 209, 210, 211, 300):
            with self.subTest(size=size):
                image = ota.inspect_image(raw_image(size), 1)
                serial = FakeReceiverSerial()
                host = ota.Uploader(serial, image, session=7, mode="fast", clock=Clock(),
                                    sleep=lambda _: None)
                host.upload()
                self.assertTrue(host.committed)
                self.assertEqual(serial.image, image.data)
                blocks = [frame for frame in serial.writes if frame.message_type == 0x11]
                self.assertEqual(len(blocks), (size + 99) // 100)
                for index, frame in enumerate(blocks):
                    self.assertEqual(frame.payload[13], ota.Op.FRAGMENT)
                    self.assertEqual(struct.unpack_from("<I", frame.payload, 14)[0], index * 100)
                    self.assertEqual(len(frame.payload), 30 + max(0, min(100, size - index * 100) - 10))

    def test_lost_batch_ack_retries_identical_host_frame(self):
        serial = FakeReceiverSerial(drop_first=True)
        host = uploader(serial, mode="fast", sleep=lambda _: None)
        host.upload()
        self.assertEqual(serial.image, raw_image())
        for first, second in zip(serial.writes[::2], serial.writes[1::2]):
            self.assertEqual(first, second)

    def test_fast_rejection_never_falls_back_or_commits(self):
        serial = FakeReceiverSerial(fail_op=ota.Op.FRAGMENT, status=3)
        host = uploader(serial, mode="fast", sleep=lambda _: None)
        with self.assertRaisesRegex(FanLinkError, "BLOCK_FINISH.*BadOffset"):
            host.upload()
        self.assertFalse(host.committed)
        self.assertNotIn(ota.Op.DATA, [frame.payload[13] for frame in serial.writes])
        self.assertEqual(serial.writes[-1].payload[13], ota.Op.ABORT)

    def test_wrong_fragment_ack_does_not_advance(self):
        def mutate(frame, ack):
            if frame.message_type == 0x11:
                changed = bytearray(ack)
                changed[13] = ota.Op.FRAGMENT
                struct.pack_into("<H", changed, 28, crc16_ccitt_false(changed[:28]))
                ack = bytes(changed)
            return encode_frame(0x90, frame.sequence, ack)
        host = uploader(FakeReceiverSerial(mutation=mutate), mode="fast", sleep=lambda _: None)
        with self.assertRaisesRegex(FanLinkError, "BLOCK_FINISH.*no matching"):
            host.upload()
        self.assertFalse(host.committed)

    def test_missing_fragment_nack_retries_identical_batch(self):
        serial = FakeReceiverSerial()
        original = serial.write
        batches = []
        def write(data):
            frame, = StreamParser().feed(data)
            if frame.message_type == 0x11:
                batches.append(frame)
                if len(batches) == 1:
                    request = bytearray(frame.payload[:30])
                    request[13] = ota.Op.BLOCK_FINISH
                    serial.buffer.extend(encode_frame(0x90, frame.sequence,
                        reply(request, ota.State.RECEIVING, 0, status=3)))
                    return len(data)
            return original(data)
        serial.write = write
        host = uploader(serial, mode="fast", sleep=lambda _: None)
        host.upload()
        self.assertTrue(host.committed)
        self.assertEqual(batches[0], batches[1])
        self.assertEqual(serial.image, raw_image())

    def test_persistent_missing_fragments_are_bounded_and_aborted(self):
        def mutate(frame, ack):
            if frame.message_type == 0x11:
                changed = bytearray(ack)
                changed[18] = 3
                struct.pack_into("<I", changed, 14, 0)
                struct.pack_into("<H", changed, 28, crc16_ccitt_false(changed[:28]))
                ack = bytes(changed)
            return encode_frame(0x90, frame.sequence, ack)
        serial = FakeReceiverSerial(mutation=mutate)
        host = uploader(serial, mode="fast", sleep=lambda _: None)
        with self.assertRaisesRegex(FanLinkError, "BLOCK_FINISH.*no matching"):
            host.upload()
        self.assertEqual(sum(frame.message_type == 0x11 for frame in serial.writes), 2)
        self.assertFalse(host.committed)
        self.assertEqual(serial.writes[-1].payload[13], ota.Op.ABORT)

    def test_fast_block_rejects_bad_lengths_alignment_and_offset(self):
        image = ota.inspect_image(raw_image(201), 1)
        for offset, size in ((-100, 100), (1, 100), (0, 99), (0, 101),
                             (100, 99), (200, 0), (200, 2), (300, 1)):
            with self.subTest(offset=offset, size=size), self.assertRaises(ValueError):
                ota.encode_fast_block(image, 7, offset, b"x" * size)

    def test_rate_and_mode_bounds(self):
        for kwargs in ({"rate": 0}, {"rate": -1}, {"rate": 5121}, {"mode": "auto"}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                uploader(FakeReceiverSerial(), **kwargs)

    def test_pacing_caps_attempts_without_catchup_after_stall(self):
        class ManualClock:
            now = 0.0
            def __call__(self):
                return self.now
            def sleep(self, seconds):
                self.now += seconds
        timer = ManualClock()
        serial = FakeReceiverSerial()
        original = serial.write
        times = []
        def write(data):
            frame, = StreamParser().feed(data)
            if frame.message_type == 0x11:
                times.append(timer.now)
            return original(data)
        serial.write = write
        image = ota.inspect_image(raw_image(500), 1)
        def progress(done, size):
            if done == 200:
                timer.now += 1.0
        host = ota.Uploader(serial, image, session=7, mode="fast", clock=timer,
                            sleep=timer.sleep, progress=progress)
        host.upload()
        self.assertEqual(len(times), 5)
        for left, right in zip(times, times[1:]):
            self.assertGreaterEqual(right - left + 1e-12, 100 / 5120)
        self.assertGreater(times[2] - times[1], 0.9)
        self.assertAlmostEqual(times[3] - times[2], 100 / 5120)

    def test_5120_ceiling_counts_nack_retransmissions(self):
        class ManualClock:
            now = 0.0
            def __call__(self): return self.now
            def sleep(self, seconds): self.now += seconds
        timer = ManualClock()
        times = []
        def mutation(frame, ack):
            if frame.message_type == ota.HOST_FAST_REQUEST:
                times.append(timer.now)
                if len(times) == 1:
                    changed = bytearray(ack)
                    changed[18] = 3
                    struct.pack_into("<I", changed, 14, 0)
                    struct.pack_into("<H", changed, 28, crc16_ccitt_false(changed[:28]))
                    ack = bytes(changed)
            return encode_frame(ota.HOST_REPLY, frame.sequence, ack)
        serial = FakeReceiverSerial(mutation=mutation)
        host = ota.Uploader(serial, ota.inspect_image(raw_image(), 1), mode="fast", session=7,
                            clock=timer, sleep=timer.sleep)
        host.upload()
        self.assertEqual(len(times), 3)
        self.assertAlmostEqual(times[1]-times[0], 100/5120)
        self.assertAlmostEqual(times[2]-times[1], 100/5120)


class AttemptStatsTests(unittest.TestCase):
    def test_timeout_then_ack_stats_and_frame_identity(self):
        events = []
        serial = FakeReceiverSerial(drop_first=True)
        host = uploader(serial, mode="fast", sleep=lambda _: None, observer=events.append)
        host.upload()
        self.assertTrue(host.committed)
        self.assertEqual(len(events), len(serial.writes))
        self.assertEqual(host.stats["attempts"], len(events))
        for first, second in zip(events[::2], events[1::2]):
            self.assertEqual((first["outcome"], second["outcome"]), ("timeout", "ack"))
            self.assertEqual((first["attempt"], second["attempt"]), (1, 2))
            self.assertEqual(first["sequence"], second["sequence"])
            self.assertIsNone(first["status"])
            self.assertIsNone(first["ack_offset"])
            self.assertGreater(first["rtt"], 0)
            self.assertGreaterEqual(second["elapsed"], first["elapsed"])
        self.assertEqual(host.stats["timeouts"], host.stats["retries"])
        self.assertEqual(host.stats["acks"], len(events) // 2)
        self.assertEqual(host.stats["bad_offsets"], 0)
        block = next(event for event in events if event["operation"] == "BLOCK_FINISH")
        self.assertEqual((block["offset"], block["block_offset"], block["expected_offset"]), (100, 0, 100))

    def test_recoverable_nack_is_not_timeout_and_records_partial_offset(self):
        events = []
        for nack_offset in (0, 10, 100):
            events.clear()
            injected = False
            def mutate(frame, ack):
                nonlocal injected
                if frame.message_type == ota.HOST_FAST_REQUEST and not injected:
                    injected = True
                    changed = bytearray(ack)
                    changed[18] = 3
                    struct.pack_into("<I", changed, 14, nack_offset)
                    struct.pack_into("<H", changed, 28, crc16_ccitt_false(changed[:28]))
                    ack = bytes(changed)
                return encode_frame(ota.HOST_REPLY, frame.sequence, ack)
            host = uploader(FakeReceiverSerial(mutation=mutate), mode="fast",
                            sleep=lambda _: None, observer=events.append)
            host.upload()
            self.assertEqual((host.stats["bad_offsets"], host.stats["timeouts"], host.stats["retries"]), (1, 0, 1))
            nack = next(event for event in events if event["outcome"] == "bad_offset")
            self.assertEqual((nack["status"], nack["ack_offset"], nack["block_offset"]), (3, nack_offset, 0))

    def test_observer_errors_and_mutation_cannot_break_commit_or_stats(self):
        def observer(event):
            event["stats"]["acks"] = -1
            raise OSError("log full")
        host = uploader(FakeReceiverSerial(), observer=observer)
        host.upload()
        self.assertTrue(host.committed)
        self.assertTrue(host.commit_sent)
        stats = host.stats
        self.assertEqual(stats["observer_errors"], stats["attempts"])
        self.assertEqual(stats["acks"], stats["attempts"])
        stats["acks"] = -1
        self.assertGreater(host.stats["acks"], 0)

    def test_progress_abort_still_prevents_finish_commit(self):
        events = []
        def stop(done, size):
            raise FanLinkError("probe gate")
        serial = FakeReceiverSerial()
        host = uploader(serial, progress=stop, observer=events.append)
        with self.assertRaisesRegex(FanLinkError, "probe gate"):
            host.upload()
        self.assertFalse(host.commit_sent)
        self.assertEqual(events[-1]["operation"], "ABORT")
        self.assertNotIn(ota.Op.FINISH, [frame.payload[13] for frame in serial.writes])

    def test_rejection_short_write_and_commit_timeout_are_recorded(self):
        events = []
        host = uploader(FakeReceiverSerial(fail_op=ota.Op.DATA), observer=events.append)
        with self.assertRaises(FanLinkError):
            host.upload()
        self.assertEqual(host.stats["rejections"], 1)
        self.assertTrue(any(event["outcome"] == "rejected" for event in events))
        serial = FakeReceiverSerial()
        serial.write = lambda data: 0
        host = uploader(serial)
        with self.assertRaisesRegex(FanLinkError, "short serial write"):
            host.upload()
        self.assertEqual((host.stats["attempts"], host.stats["io_errors"]), (2, 2))
        events.clear()
        host = uploader(FakeReceiverSerial(commit_loss=True), observer=events.append)
        with self.assertRaisesRegex(FanLinkError, "COMMIT.*no matching"):
            host.upload()
        self.assertTrue(host.commit_sent)
        self.assertFalse(host.committed)
        self.assertEqual([event["outcome"] for event in events[-2:]], ["timeout", "timeout"])
        self.assertNotIn("ABORT", [event["operation"] for event in events])

    def test_invalid_and_unmatched_acks_count_without_acceptance(self):
        def mutate(frame, ack):
            return (encode_frame(ota.HOST_REPLY, frame.sequence, ack[:-1]) +
                    encode_frame(ota.HOST_REPLY, (frame.sequence + 1) & 65535, ack) +
                    encode_frame(ota.HOST_REPLY, frame.sequence, ack))
        host = uploader(FakeReceiverSerial(mutation=mutate))
        host.upload()
        self.assertEqual(host.stats["invalid_acks"], host.stats["acks"])
        self.assertEqual(host.stats["ignored_frames"], host.stats["acks"])
        self.assertEqual(host.stats["parsed_frames"], host.stats["acks"] * 3)


class SerialTests(unittest.TestCase):
    def test_lazy_open_sets_control_lines_before_open_and_exclusive(self):
        events = []

        class Serial:
            def __init__(self, **kwargs):
                events.append(("construct", kwargs))

            def __setattr__(self, name, value):
                events.append((name, value))

            def open(self):
                events.append(("open",))

        with patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=Serial)}):
            ota.open_serial("TEST", 115200)
        self.assertTrue(events[0][1]["exclusive"])
        self.assertIsNone(events[0][1]["port"])
        self.assertEqual(events[1:], [("dtr", False), ("rts", False), ("port", "TEST"), ("open",)])

    def test_open_failure_does_not_retry_or_stop_owner(self):
        events = []

        class Serial:
            def __init__(self, **kwargs):
                pass

            def open(self):
                events.append("open")
                raise OSError("busy")

            def close(self):
                events.append("close")

        with patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=Serial)}):
            with self.assertRaisesRegex(FanLinkError, "exclusively.*busy"):
                ota.open_serial("TEST", 115200)
        self.assertEqual(events, ["open", "close"])


class CommandTests(unittest.TestCase):
    def test_success_and_uncertain_commit_have_distinct_exit_and_output(self):
        for commit_loss in (False, True):
            class Port(FakeReceiverSerial):
                def __enter__(self):
                    return self

                def __exit__(self, *args):
                    pass

            port = Port(commit_loss=commit_loss)
            original_uploader = ota.Uploader

            def make_uploader(serial, image, **kwargs):
                return original_uploader(serial, image, session=7, attempts=2,
                                         timeout=0.05, begin_timeout=0.05,
                                         clock=Clock(), **kwargs)

            with self.subTest(commit_loss=commit_loss), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "firmware.bin"
                path.write_bytes(raw_image())
                output, errors = io.StringIO(), io.StringIO()
                with patch.object(ota, "open_serial", return_value=port), patch.object(
                        ota, "Uploader", side_effect=make_uploader), redirect_stdout(output), redirect_stderr(errors):
                    result = ota.main(["--port", "TEST", "--firmware", str(path), "--node", "1"])
                if commit_loss:
                    self.assertEqual(result, 2)
                    self.assertNotIn("COMMIT ACK received", output.getvalue())
                    self.assertIn("COMMIT was sent but not confirmed", errors.getvalue())
                else:
                    self.assertEqual(result, 0)
                    self.assertIn("COMMIT ACK received", output.getvalue())
                    self.assertIn("NOT yet verified", output.getvalue())


if __name__ == "__main__":
    unittest.main()
