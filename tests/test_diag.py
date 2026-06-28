import struct
import unittest

from qdiagparser.diag import decode_frame, generate_packet, parse_dci_log_record, parse_log_packet


class DiagTests(unittest.TestCase):
    def test_generate_and_decode_packet_roundtrip(self):
        payload = b"\x10\x7d\x7ehello"
        frame = generate_packet(payload)
        self.assertTrue(frame.endswith(b"\x7e"))
        self.assertEqual(decode_frame(frame[:-1]), payload)

    def test_parse_log_packet_header(self):
        body = b"\x05payload"
        length2 = 12 + len(body)
        payload = struct.pack("<BBHHHQ", 0x10, 0, length2, length2, 0xB17F, 0) + body
        pkt = parse_log_packet(payload)
        self.assertIsNotNone(pkt)
        self.assertEqual(pkt.log_id, 0xB17F)
        self.assertEqual(pkt.body, body)

    def test_parse_qmdl2_wrapped_log_packet(self):
        body = b"\x01payload"
        length2 = 12 + len(body)
        diag_log = struct.pack("<BBHHHQ", 0x10, 0, length2, length2, 0xB193, 0) + body
        pkt = parse_log_packet(b"\x98\x01\x00\x00\x02\x00\x00\x00" + diag_log)
        self.assertIsNotNone(pkt)
        self.assertEqual(pkt.radio_id, 2)
        self.assertEqual(pkt.log_id, 0xB193)
        self.assertEqual(pkt.body, body)

    def test_parse_dci_log_record(self):
        body = b"\x01payload"
        record = struct.pack("<HHLL", 12 + len(body), 0xB193, 0, 0) + body
        pkt = parse_dci_log_record(record)
        self.assertIsNotNone(pkt)
        self.assertEqual(pkt.log_id, 0xB193)
        self.assertEqual(pkt.body, body)


if __name__ == "__main__":
    unittest.main()
