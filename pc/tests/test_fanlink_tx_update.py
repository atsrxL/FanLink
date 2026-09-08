"""TX-local host transport and explicit reboot/ROM-entry fault tests, no hardware."""

from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import fanlink_ota as ota
import fanlink_tx_control as control
from fanlink_cli import FanLinkError, StreamParser, encode_frame, crc16_ccitt_false
from test_fanlink_ota import Clock, FakeReceiverSerial, raw_image


class TxUploadTests(unittest.TestCase):
    def image(self):
        return ota.inspect_image(raw_image(role=1), 1, target="tx")

    def host(self, serial, **kwargs):
        return ota.Uploader(serial, self.image(), target="tx", session=7, attempts=2,
                            timeout=0.05, begin_timeout=0.05, clock=Clock(),
                            sleep=lambda _: None, **kwargs)

    def test_explicit_tx_role_required_even_for_programmatic_upload(self):
        with self.assertRaises(FanLinkError):
            ota.Uploader(None, self.image())
        with self.assertRaises(FanLinkError):
            ota.Uploader(None, ota.inspect_image(raw_image(), 1), target="tx")
        with self.assertRaises(FanLinkError):
            ota.Uploader(None, replace(self.image(), compatibility=1), target="tx")
        for data, node, network in ((raw_image(), 1, None), (raw_image(role=1, node=2), 2, None),
                                    (raw_image(role=1), 1, 99)):
            with self.assertRaises(FanLinkError):
                ota.inspect_image(data, node, network, target="tx")

    def test_tx_transfer_legacy_fast_and_idempotent_loss(self):
        for mode in ("legacy", "fast"):
            serial = FakeReceiverSerial(target="tx", drop_first=True)
            host = self.host(serial, mode=mode)
            host.upload()
            self.assertTrue(host.committed)
            self.assertEqual(serial.image, raw_image(role=1))
            self.assertEqual(serial.commit_count, 1)
            self.assertEqual({f.message_type for f in serial.writes},
                             {0x12, 0x13} if mode == "fast" else {0x12})
            for first, second in zip(serial.writes[::2], serial.writes[1::2]):
                self.assertEqual(first, second)

    def test_tx_never_accepts_rf_ack_or_bad_identity_crc(self):
        def wrong_type(frame, ack):
            return encode_frame(0x90, frame.sequence, ack)
        mutations = [wrong_type]
        for offset in (4, 8, 12, 13, 14, 19, 20, 24, 28):
            def mutate(frame, ack, offset=offset):
                data = bytearray(ack)
                data[offset] ^= 1
                if offset != 28:
                    struct.pack_into("<H", data, 28, crc16_ccitt_false(data[:28]))
                return encode_frame(0x91, frame.sequence, data)
            mutations.append(mutate)
        for mutation in mutations:
            serial = FakeReceiverSerial(target="tx", mutation=mutation)
            host = self.host(serial)
            with self.assertRaises(FanLinkError):
                host.upload()
            self.assertFalse(host.committed)
            self.assertEqual(serial.commit_count, 0)

    def test_tx_lost_commit_is_unknown_without_abort(self):
        serial = FakeReceiverSerial(target="tx", commit_loss=True)
        host = self.host(serial)
        with self.assertRaisesRegex(FanLinkError, "COMMIT"):
            host.upload()
        self.assertTrue(host.commit_sent)
        self.assertFalse(host.committed)
        self.assertNotIn(ota.Op.ABORT, [f.payload[13] for f in serial.writes])

    def test_tx_cli_inspection_and_rejections_never_open_uart(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tx.bin"
            path.write_bytes(raw_image(role=1))
            base = ["--firmware", str(path), "--target", "tx", "--node", "1", "--inspect"]
            with patch.object(ota, "open_serial", side_effect=AssertionError("UART")), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(ota.main(base), 0)
                self.assertEqual(ota.main(["--firmware", str(path), "--node", "1", "--inspect"]), 2)
                for extra in (["--node", "2"], ["--network", "-1"], ["--baud", "0"], ["--rate", "5121"]):
                    with self.assertRaises(SystemExit):
                        ota.main(base + extra)


class ControlSerial:
    def __init__(self, mutation=None, status=0):
        self.mutation, self.status = mutation, status
        self.buffer = bytearray()
        self.writes = []

    @property
    def in_waiting(self):
        return len(self.buffer)

    def write(self, data):
        frame, = StreamParser().feed(data)
        self.writes.append(frame)
        response = encode_frame(0x92, frame.sequence, frame.payload + bytes([self.status]))
        self.buffer.extend(self.mutation(frame, response) if self.mutation else response)
        return len(data)

    def read(self, count):
        result = bytes(self.buffer[:count])
        del self.buffer[:count]
        return result

    def flush(self):
        pass


class ControlTests(unittest.TestCase):
    def request(self, serial, action="boot"):
        return control.request_control(serial, 0x10203040, action, session=7, sequence=9,
                                       clock=Clock(), timeout=0.05)

    def test_exact_reboot_and_boot_wire(self):
        for action, code in (("reboot", 1), ("boot", 2)):
            serial = ControlSerial()
            self.request(serial, action)
            self.assertEqual(len(serial.writes), 1)
            frame = serial.writes[0]
            self.assertEqual((frame.message_type, frame.sequence), (0x14, 9))
            self.assertEqual(frame.payload, struct.pack("<IIB", 0x10203040, 7, code) + b"FLTXCTL")

    def test_nacks_are_not_success(self):
        for status, text in ((1, "busy"), (2, "unsupported")):
            with self.assertRaisesRegex(FanLinkError, text):
                self.request(ControlSerial(status=status))

    def test_wrong_echo_type_sequence_crc_status_timeout_never_retries(self):
        mutations = [lambda f, r: b"", lambda f, r: r[:-1] + bytes([r[-1] ^ 1]),
                     lambda f, r: encode_frame(0x91, f.sequence, f.payload + b"\0"),
                     lambda f, r: encode_frame(0x92, f.sequence + 1, f.payload + b"\0"),
                     lambda f, r: encode_frame(0x92, f.sequence, f.payload + b"\x03"),
                     lambda f, r: encode_frame(0x92, f.sequence, f.payload)]
        for offset in range(16):
            def mutate(f, r, offset=offset):
                payload = bytearray(f.payload + b"\0")
                payload[offset] ^= 1
                return encode_frame(0x92, f.sequence, payload)
            mutations.append(mutate)
        for mutation in mutations:
            serial = ControlSerial(mutation)
            with self.assertRaisesRegex(FanLinkError, "outcome unknown"):
                self.request(serial)
            self.assertEqual(len(serial.writes), 1)

    def test_invalid_arguments_do_not_write(self):
        for network, action, kwargs in ((-1, "boot", {}), (1, "auto", {}),
                                       (1, "boot", {"session": 0}), (1, "boot", {"timeout": float("nan")})):
            serial = ControlSerial()
            with self.assertRaises(FanLinkError):
                control.request_control(serial, network, action, **kwargs)
            self.assertEqual(serial.writes, [])

    def test_short_write_does_not_retry_or_claim_acceptance(self):
        serial = ControlSerial()
        write = serial.write
        serial.write = lambda data: write(data) - 1
        with self.assertRaisesRegex(FanLinkError, "short serial write.*unknown"):
            self.request(serial)
        self.assertEqual(len(serial.writes), 1)

    def test_cli_acceptance_releases_port_but_does_not_claim_boot_verified(self):
        class Port(ControlSerial):
            closed = False
            def __enter__(self):
                return self
            def __exit__(self, *args):
                self.closed = True
        port = Port()
        output = io.StringIO()
        with patch.object(control, "open_serial", return_value=port), redirect_stdout(output):
            self.assertEqual(control.main(["boot", "--port", "TEST", "--network", "1"]), 0)
        self.assertTrue(port.closed)
        self.assertEqual(len(port.writes), 1)
        self.assertIn("NOT yet verified", output.getvalue())
        self.assertIn("No flasher was started", output.getvalue())

    def test_cli_invalid_arguments_do_not_open(self):
        with patch.object(control, "open_serial", side_effect=AssertionError("UART")), redirect_stderr(io.StringIO()):
            for args in ([], ["auto", "--port", "TEST", "--network", "1"],
                         ["boot", "--port", "TEST", "--network", "-1"],
                         ["reboot", "--port", "TEST", "--network", "1", "--baud", "0"]):
                with self.assertRaises(SystemExit):
                    control.main(args)


if __name__ == "__main__":
    unittest.main()
