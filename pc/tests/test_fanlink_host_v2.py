"""Host v2 only: eight slots, strict snapshots, no legacy wire adapter."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import fanlink_cli as wire, fanlink_panel as panel


def old_version_frame(message_type, payload):
    frame = bytearray(wire.encode_frame(message_type, 1, payload))
    frame[2] = 1
    struct.pack_into("<H", frame, len(frame) - 2, wire.crc16_ccitt_false(frame[:-2]))
    return bytes(frame)


class HostV2Tests(unittest.TestCase):
    def test_version_and_exact_native_eight_frame_sizes(self):
        self.assertEqual((wire.VERSION, wire.NODE_COUNT, wire.MAX_PAYLOAD), (2, 8, 128))
        self.assertEqual((wire.STATUS_PAYLOAD_SIZE, wire.LINK_METRICS_PAYLOAD_SIZE), (54, 16))
        for kind, payload_size, frame_size in ((wire.SET_ALL_DUTY, 8, 18),
                (wire.STATUS_SNAPSHOT, 54, 64), (wire.LINK_METRICS_SNAPSHOT, 16, 26)):
            with self.subTest(kind=kind):
                encoded = wire.encode_frame(kind, 7, bytes(payload_size))
                self.assertEqual(encoded[:4], b"FL\x02" + bytes((kind,)))
                self.assertEqual(struct.unpack_from("<H", encoded, 4)[0], payload_size)
                self.assertEqual(len(encoded), frame_size)
                self.assertEqual(list(wire.StreamParser().feed(encoded)),
                                 [wire.Frame(kind, 7, bytes(payload_size))])
        # Large OTA/diagnostic envelopes remain supported, not clipped to STATUS size.
        encoded = wire.encode_frame(0x11, 8, bytes(128))
        self.assertEqual(len(list(wire.StreamParser().feed(encoded))[0].payload), 128)
        with self.assertRaises(ValueError):
            wire.encode_frame(0x11, 8, bytes(129))

    def test_crc_valid_v1_rejected_and_v2_resynchronizes(self):
        for kind, sizes in ((wire.SET_ALL_DUTY, (8, 16)),
                            (wire.STATUS_SNAPSHOT, (54, 102)),
                            (wire.LINK_METRICS_SNAPSHOT, (16, 32))):
            for size in sizes:
                with self.subTest(kind=kind, size=size):
                    parser = wire.StreamParser()
                    old = old_version_frame(kind, bytes(size))
                    good = wire.encode_frame(wire.HEARTBEAT, 2)
                    frames = []
                    for value in old + good:
                        frames.extend(parser.feed(bytes((value,))))
                    self.assertEqual(frames, [wire.Frame(wire.HEARTBEAT, 2, b"")])
                    self.assertGreaterEqual(parser.rejected_frames, 1)

    def test_snapshot_lengths_reject_legacy_or_partial_payloads(self):
        for decoder, valid, invalid in ((wire.decode_status_snapshot, 54, (53, 55, 102)),
                (wire.decode_link_metrics_snapshot, 16, (15, 17, 32))):
            self.assertEqual([n.node_id for n in decoder(bytes(valid)).nodes], list(range(1, 9)))
            for size in invalid:
                with self.subTest(decoder=decoder.__name__, size=size), self.assertRaises(wire.FanLinkError):
                    decoder(bytes(size))

    def test_old_or_wrong_length_status_cannot_refresh_panel_or_recover_presence(self):
        now = [100.0]
        host = panel.FanLinkPanel(monotonic=lambda: now[0], auto_start_demo=False)
        host._connected = True
        payload = bytearray(54)
        payload[1] = 8
        struct.pack_into("<HBBBB", payload, 6, 1400, 0, 255, 0, 0)
        try:
            host.ingest_serial_bytes(wire.encode_frame(wire.STATUS_SNAPSHOT, 1, payload))
            host.ingest_serial_bytes(wire.encode_frame(wire.LINK_METRICS_SNAPSHOT, 2, bytes(16)))
            self.assertTrue(host.state()["nodes"][0]["online"])
            last_status = host._last_status_monotonic
            last_metrics = host._last_link_metrics_monotonic
            now[0] += 3
            for kind, size in ((wire.STATUS_SNAPSHOT, 54), (wire.STATUS_SNAPSHOT, 102),
                              (wire.LINK_METRICS_SNAPSHOT, 16), (wire.LINK_METRICS_SNAPSHOT, 32)):
                host.ingest_serial_bytes(old_version_frame(kind, bytes(size)))
            for kind, size in ((wire.STATUS_SNAPSHOT, 102), (wire.LINK_METRICS_SNAPSHOT, 32)):
                host.ingest_serial_bytes(wire.encode_frame(kind, 3, bytes(size)))
            state = host.state()
            self.assertFalse(state["nodes"][0]["online"])
            self.assertTrue(state["nodes"][0]["stale"])
            self.assertEqual(state["nodes"][0]["rpm"], 1400)
            self.assertEqual(host._last_status_monotonic, last_status)
            self.assertEqual(host._last_link_metrics_monotonic, last_metrics)
            self.assertEqual(host._status_decode_errors, 1)
            self.assertEqual(host._link_metrics_decode_errors, 1)
            host.ingest_serial_bytes(wire.encode_frame(wire.STATUS_SNAPSHOT, 4, payload))
            self.assertTrue(host.state()["nodes"][0]["online"])
        finally:
            host.close()


if __name__ == "__main__":
    unittest.main()
