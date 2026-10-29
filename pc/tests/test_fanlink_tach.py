import importlib.util
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import fanlink_tach as tach
from fanlink_cli import FanLinkError, StreamParser, crc16_ccitt_false, encode_frame

def response(network=7, session=123, node=1, flags=1, age=3):
    body = b"\xf1\xa5\x01\x43" + struct.pack("<IIBBIIIH", network, session, node, flags, 50, 20, 9, age)
    return body + struct.pack("<H", crc16_ccitt_false(body))

class Port:
    def __init__(self, answer=True): self.bytes = b""; self.now = 0; self.answer=answer; self.writes=[]
    def write(self, data):
        self.writes.append(data)
        frame = next(StreamParser().feed(data))
        net, sess, node = struct.unpack_from("<IIB", frame.payload, 4)
        if self.answer:
            for payload, seq in ((response(net,sess,node),frame.sequence^1),
                    (response(net,sess+1,node),frame.sequence),
                    (response(net+1,sess,node),frame.sequence),
                    (response(net,sess,2),frame.sequence),
                    (response(net,sess,node),frame.sequence)):
                self.bytes += encode_frame(tach.HOST_REPLY,seq,payload)
        return len(data)
    def flush(self): pass
    def read(self, count):
        self.now += 0.01
        data,self.bytes=self.bytes[:count],self.bytes[count:]
        return data
    @property
    def in_waiting(self): return len(self.bytes)

class Tests(unittest.TestCase):
    def test_codec(self):
        self.assertEqual(len(tach.encode_request(7,123,1)),30)
        for values in ((7,0,1),(7,1,0),(7,1,9),(-1,1,1)):
            with self.assertRaises(FanLinkError): tach.encode_request(*values)
        reply=tach.decode_reply(response())
        self.assertEqual((reply.raw_edges,reply.valid_periods,reply.filter_rejects),(50,20,9))
        self.assertIsNone(tach.decode_reply(response(age=65535)).last_edge_age_ms)
        self.assertEqual(tach.decode_reply(response(age=65534)).last_edge_age_ms,65534)
        for i in range(30):
            bad=bytearray(response());bad[i]^=1
            with self.assertRaises(FanLinkError): tach.decode_reply(bad)
        for payload in (response(flags=2),response(session=0),response(node=9),response()[:-1]):
            with self.assertRaises(FanLinkError): tach.decode_reply(payload)
    def test_match_and_single_request(self):
        port=Port(); client=tach.Client(port,7,clock=lambda:port.now)
        self.assertEqual(client.query(1).node,1); self.assertEqual(len(port.writes),1)
        frame=next(StreamParser().feed(port.writes[0]))
        self.assertEqual(frame.message_type,0x16)
    def test_timeout_not_zero(self):
        port=Port(False); client=tach.Client(port,7,clock=lambda:port.now)
        with self.assertRaisesRegex(FanLinkError,"not zero"): client.query(1)
        self.assertEqual(len(port.writes),1)
    def test_invalid_cli_never_opens(self):
        for tail in (("--network","-1"),("--interval","nan"),("--interval","inf"),("--baud","0")):
            with (patch.object(sys,"argv",["tach","--port","/fake","--node","1","--network","7",*tail]),
                    patch.object(tach,"open_port") as opened):
                with self.assertRaises(SystemExit): tach.main()
                opened.assert_not_called()
    def test_wiring_guards(self):
        root=Path(__file__).resolve().parents[2]
        tx=(root/"firmware/src/main_tx.cpp").read_text()
        self.assertIn("tachBridge.startAfterSlot();",tx)
        self.assertIn("if (tachBridge.active()) {\n    (void)fanlink::handleHostControlFrame(frame, controller);",tx)
        self.assertIn("if (tachBridge.waiting()) { yield(); return; }",tx)
        rx=(root/"firmware/src/main_rx.cpp").read_text()
        self.assertIn("tachdiag::RxMaintenance maintenance",rx)
        self.assertIn("(void)tachometer.diagnostics(clockPort.micros());",rx)
        self.assertIn("static_cast<std::uint32_t>(nowMs - lastTachDiagnosticAgeMs) >= 1000U",rx)
    def test_uart_open_reset_lines_disabled(self):
        class Connection:
            def __init__(self, **kwargs):
                self.kwargs=kwargs; self.dtr=True; self.rts=True; self.opened=False
                self.assert_no_open=kwargs["port"] is None
            def open(self):
                if self.dtr or self.rts: raise AssertionError("reset line enabled")
                self.opened=True
        with patch.dict(sys.modules,{"serial":SimpleNamespace(Serial=Connection)}):
            port=tach.open_port("/fake",115200)
            self.assertTrue(port.opened and port.assert_no_open)
            self.assertEqual(port.port,"/fake")

if __name__ == "__main__": unittest.main()
