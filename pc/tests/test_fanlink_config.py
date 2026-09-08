"""Config protocol fault tests: fake serial only, no hardware or pyserial."""
from contextlib import redirect_stdout
import io
from pathlib import Path
import struct
import sys
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import fanlink_config as config
from fanlink_cli import FanLinkError, StreamParser, encode_frame, crc16_ccitt_false


class Clock:
    def __init__(self): self.now = 0.0
    def __call__(self):
        self.now += 0.01
        return self.now


def reply(request, *, status=0, profile=2, capabilities=120, flags=0, mode=None):
    body = bytearray(request)
    body[3] = 0x41
    node = body[12]
    selected = (255 if node == 0 else 1) if mode is None else mode
    if body[13] == config.SET_MODE and not status:
        selected = body[14]
    power = -6 if profile == 2 else 13
    if body[13] == config.SET_POWER and not status:
        power = struct.unpack_from("<b", body, 14)[0]
    struct.pack_into("<BBBbBIHBB", body, 15, status, profile, selected,
                     power, flags, capabilities, 5120, 8, 125)
    struct.pack_into("<H", body, 28, crc16_ccitt_false(body[:28]))
    return bytes(body)


class Serial:
    def __init__(self, mutate=None, **reply_options):
        self.buffer = bytearray()
        self.frames = []
        self.mutate = mutate
        self.options = reply_options
    @property
    def in_waiting(self): return len(self.buffer)
    def write(self, data):
        frame, = StreamParser().feed(data)
        self.frames.append(frame)
        assert frame.message_type == 0x15 and len(frame.payload) == 30
        assert frame.payload[15:28] == bytes(13)
        response = reply(frame.payload, **self.options)
        packet = encode_frame(0x93, frame.sequence, response)
        if self.mutate: packet = self.mutate(frame, response, packet)
        self.buffer.extend(packet)
        return len(data)
    def read(self, count):
        result = bytes(self.buffer[:count]); del self.buffer[:count]; return result
    def flush(self): pass


def client(serial):
    return config.Client(serial, 42, attempts=2, timeout=0.05, clock=Clock())


class ProtocolTests(unittest.TestCase):
    def test_query_and_signed_power_roundtrip(self):
        for node in (0, 1, 8):
            packet = config.encode_request(42, 7, node, config.SET_POWER, -6)
            self.assertEqual(len(packet), 30)
            result = config.decode_reply(reply(packet))
            self.assertEqual((result.node, result.value, result.power), (node, 250, -6))
    def test_reject_invalid_requests(self):
        for values in ((42, 0, 1, 1, 0), (42, 7, 9, 1, 0), (42, 7, 0, 2, 1),
                       (42, 7, 1, 2, 254), (42, 7, 1, 1, 1), (42, 7, 1, 3, 14),
                       (-1, 7, 1, 1, 0), (42, 7, 1, 9, 0)):
            with self.subTest(values=values), self.assertRaises(FanLinkError):
                config.encode_request(*values)
    def test_reject_malformed_reply_fields(self):
        original = reply(config.encode_request(42, 7, 1, 1))
        for index, value in ((0, 0), (2, 2), (3, 0x40), (12, 9), (13, 9),
                             (15, 6), (16, 3), (17, 255), (18, 127), (19, 4),
                             (23, 128), (24, 1), (26, 16), (27, 100)):
            changed = bytearray(original); changed[index] = value
            struct.pack_into("<H", changed, 28, crc16_ccitt_false(changed[:28]))
            with self.subTest(index=index), self.assertRaises(FanLinkError):
                config.decode_reply(bytes(changed))
        for data in (original[:-1], original + b"x", original[:-1] + b"x"):
            with self.assertRaises(FanLinkError): config.decode_reply(data)
    def test_safe_mode_requires_corrupt_record_flag(self):
        packet = config.encode_request(42, 7, 1, 1)
        self.assertEqual(config.decode_reply(reply(packet, mode=254, flags=2)).mode, 254)
        with self.assertRaises(FanLinkError): config.decode_reply(reply(packet, mode=254))


class TransactionTests(unittest.TestCase):
    def test_query_before_mutation_and_unique_transaction(self):
        serial = Serial()
        host = client(serial)
        with redirect_stdout(io.StringIO()): host.set_value(1, config.SET_POWER, -3)
        self.assertEqual([f.payload[13] for f in serial.frames], [1, 3])
        self.assertNotEqual(serial.frames[0].payload[8:12], serial.frames[1].payload[8:12])
        self.assertTrue(host.mutation_sent)
    def test_capabilities_and_board_power_gate(self):
        for options, operation, value in (({"capabilities": 0}, 3, -6),
                ({"profile": 2}, 3, 13), ({"profile": 1}, 3, 14),
                ({"flags": 1}, 3, -6)):
            serial = Serial(**options)
            with redirect_stdout(io.StringIO()), self.assertRaises(FanLinkError):
                client(serial).set_value(1, operation, value)
            self.assertEqual(len(serial.frames), 1)
    def test_bad_statuses_are_fatal_no_hidden_retry(self):
        for status in range(1, 6):
            serial = Serial(status=status)
            with self.subTest(status=status), self.assertRaisesRegex(FanLinkError, config.STATUS[status]):
                client(serial).query(1)
            self.assertEqual(len(serial.frames), 1)
    def test_lost_ack_retries_identical_request(self):
        count = [0]
        def mutate(frame, response, packet):
            count[0] += 1
            return b"" if count[0] == 1 else packet
        serial = Serial(mutate)
        self.assertEqual(client(serial).query(1).node, 1)
        self.assertEqual(serial.frames[0], serial.frames[1])
    def test_wrong_identity_sequence_type_and_crc_timeout(self):
        for index in (4, 8, 12, 13, 14):
            def mutate(frame, response, packet):
                changed = bytearray(response); changed[index] ^= 1
                struct.pack_into("<H", changed, 28, crc16_ccitt_false(changed[:28]))
                return encode_frame(0x93, frame.sequence, changed)
            serial = Serial(mutate)
            with self.subTest(index=index), self.assertRaisesRegex(FanLinkError, "timeout"):
                client(serial).query(1)
            self.assertEqual(len(serial.frames), 2)
        for mutate in (lambda f,r,p: encode_frame(0x93, (f.sequence+1)&65535, r),
                         lambda f,r,p: encode_frame(0x90, f.sequence, r),
                         lambda f,r,p: p[:-1] + bytes([p[-1] ^ 1]), lambda f,r,p: b""):
            with self.assertRaisesRegex(FanLinkError, "timeout"): client(Serial(mutate)).query(1)
    def test_noise_and_stale_ack_before_matching_reply(self):
        serial = Serial(lambda f,r,p: b"noise" + encode_frame(0x93, (f.sequence-1)&65535, r) + p)
        self.assertEqual(client(serial).query(8).node, 8)
    def test_short_write_and_pending_ack_not_reboot_proof(self):
        serial = Serial(flags=1)
        host = client(serial)
        ack = host.exchange(1, config.SET_POWER, -6)
        self.assertTrue(ack.flags & 1)
        serial.write = lambda _: 1
        with self.assertRaisesRegex(FanLinkError, "short"): host.query(1)
    def test_serial_open_never_accesses_control_lines(self):
        calls = []
        def constructor(*args, **kwargs):
            calls.append((args, kwargs)); return object()
        with patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=constructor)}):
            config.open_serial("TEST", 115200)
        self.assertEqual(calls, [(("TEST",), dict(baudrate=115200, timeout=0.05,
            write_timeout=1.0, exclusive=True))])


if __name__ == "__main__": unittest.main()
