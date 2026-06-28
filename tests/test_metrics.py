import struct
import unittest

from qdiagparser.diag import DiagLogPacket, parse_qxdm_ts
from qdiagparser.metrics import MetricsParser


class MetricsTests(unittest.TestCase):
    def test_lte_serving_cell_meas_v5(self):
        body = bytes([5]) + struct.pack(
            "<BHLH2xLLLLLL",
            1,
            0,
            1300,
            123,
            1600,
            1584,
            200 | (180 << 20),
            400 << 10,
            0,
            0,
        )
        pkt = DiagLogPacket(0xB17F, parse_qxdm_ts(0), 0, body)
        events = MetricsParser().parse(pkt)
        self.assertEqual(len(events), 1)
        event = events[0]
        self.assertEqual(event["event"], "serving_cell_measurement")
        self.assertEqual(event["earfcn"], 1300)
        self.assertEqual(event["pci"], 123)
        self.assertEqual(event["rsrp_dbm"], -80.0)
        self.assertEqual(event["avg_rsrp_dbm"], -81.0)
        self.assertEqual(event["rssi_dbm"], -85.0)
        self.assertEqual(event["rsrq_db"], -17.5)
        self.assertEqual(event["avg_rsrq_db"], -18.75)

    def test_lte_ca_combo_raw_marks_uecapabilityparser_format(self):
        pkt = DiagLogPacket(0xB0CD, parse_qxdm_ts(0), 0, b"\x18\x00abc")
        event = MetricsParser().parse(pkt)[0]
        self.assertEqual(event["event"], "supported_ca_combos_raw")
        self.assertEqual(event["format"], "QLTE")
        self.assertEqual(event["payload_hex"], "1800616263")

    def test_lte_phy_pdsch_stat_candidate(self):
        rec = bytearray(40)
        struct.pack_into("<H", rec, 0, 1234)
        struct.pack_into("<H", rec, 2, 0x22)
        struct.pack_into("<H", rec, 4, 0x44)
        struct.pack_into("<H", rec, 8, 0x88)
        struct.pack_into("<H", rec, 12, 0x120)
        rec[16] = 18
        struct.pack_into("<H", rec, 18, 0x180)
        struct.pack_into("<H", rec, 20, 0x200)
        struct.pack_into("<H", rec, 24, 0x240)
        struct.pack_into("<H", rec, 28, 0x280)
        body = bytes([0xA1, 0x01]) + struct.pack("<H", 77) + bytes(rec)
        pkt = DiagLogPacket(0xB173, parse_qxdm_ts(0), 0, body)
        event = MetricsParser().parse(pkt)[0]
        self.assertEqual(event["event"], "lte_phy_pdsch_stat_candidate")
        self.assertEqual(event["record_count"], 1)
        self.assertEqual(event["tti_guess"], 1234)
        self.assertEqual(event["mcs_candidate_raw"], 18)
        self.assertEqual(event["confidence"], "candidate_unconfirmed")


if __name__ == "__main__":
    unittest.main()
