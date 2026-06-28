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

    def test_lte_phy_pusch_tx_v161_layout(self):
        rec = bytearray(100)
        flags = 1 | (1 << 2) | (1 << 3) | (2 << 7) | (1 << 12)
        alloc = 1 | (10 << 1) | (12 << 8) | (25 << 15)
        struct.pack_into("<H", rec, 0, 5678)
        struct.pack_into("<H", rec, 2, flags)
        struct.pack_into("<I", rec, 4, alloc)
        struct.pack_into("<H", rec, 8, 321)
        struct.pack_into("<H", rec, 10, 512)
        struct.pack_into("<I", rec, 36, 4)
        struct.pack_into("<I", rec, 40, 43)
        body = bytes([161, 174]) + struct.pack("<HH", 2, 1234) + b"\x00\x00" + bytes(rec)
        pkt = DiagLogPacket(0xB139, parse_qxdm_ts(0), 0, body)
        event = MetricsParser().parse(pkt)[0]
        self.assertEqual(event["event"], "lte_phy_pusch_tx_candidate")
        self.assertEqual(event["version"], 161)
        self.assertEqual(event["sfn_guess"], 567)
        self.assertEqual(event["subframe_guess"], 8)
        self.assertEqual(event["carrier_id"], 1)
        self.assertEqual(event["rv"], 1)
        self.assertEqual(event["rb_start_slot0"], 10)
        self.assertEqual(event["rb_start_slot1"], 12)
        self.assertEqual(event["rb_count"], 25)
        self.assertEqual(event["tb_size"], 321)
        self.assertEqual(event["coding_rate"], 0.5)
        self.assertEqual(event["pusch_modulation"], "16QAM")
        self.assertEqual(event["tx_power_dbm_candidate"], -85)

    def test_lte_phy_pdsch_stat_v36_layout(self):
        rec = bytearray(40)
        struct.pack_into("<H", rec, 0, (100 << 4) | 7)
        rec[2] = 50
        rec[3] = 2
        rec[4] = 1
        rec[5] = 3
        tb = 12
        rec[tb] = 5 | (2 << 4) | (1 << 6) | (1 << 7)
        rec[tb + 1] = 2
        struct.pack_into("<H", rec, tb + 4, 1000)
        rec[tb + 6] = 18
        rec[tb + 7] = 40
        rec[tb + 8] = 6
        body = bytes([36, 1, 0, 0]) + bytes(rec)
        pkt = DiagLogPacket(0xB173, parse_qxdm_ts(0), 0, body)
        event = MetricsParser().parse(pkt)[0]
        self.assertEqual(event["confidence"], "layout_v36_decoded")
        self.assertEqual(event["sfn"], 100)
        self.assertEqual(event["subframe"], 7)
        self.assertEqual(event["num_rbs"], 50)
        self.assertEqual(event["serving_cell_id"], 3)
        tb0 = event["transport_blocks"][0]
        self.assertEqual(tb0["harq_id"], 5)
        self.assertEqual(tb0["rv"], 2)
        self.assertEqual(tb0["tb_size"], 1000)
        self.assertEqual(tb0["mcs"], 18)
        self.assertEqual(tb0["num_rbs"], 40)
        self.assertEqual(tb0["modulation"], "64QAM")
        self.assertEqual(event["crc_pass_total"], 1)
        self.assertEqual(event["crc_fail_total"], 0)
        self.assertEqual(event["dl_bler"], 0.0)

    def test_nr_mac_ul_tb_stats_v21_layout(self):
        header = bytearray(20)
        struct.pack_into("<HH", header, 0, 1, 2)
        header[15] = 1
        rec = bytearray(96)
        struct.pack_into("<QQQQQQ", rec, 0, 32019, 2618, 1621, 609, 1624, 0)
        struct.pack_into("<IIIIIIIIIIHH", rec, 48, 106, 11, 0, 0, 0, 0, 101, 0, 117, 0, 230, 0)
        pkt = DiagLogPacket(0xB881, parse_qxdm_ts(0), 0, bytes(header + rec))
        event = MetricsParser().parse(pkt)[0]
        self.assertEqual(event["event"], "nr_mac_ul_tb_stats")
        self.assertEqual(event["confidence"], "layout_v2_1_decoded")
        self.assertEqual(event["tb_new_tx_bytes"], 32019)
        self.assertEqual(event["num_retx_tb"], 11)
        self.assertEqual(event["avg_mcs_candidate"], 13.85)
        self.assertEqual(event["avg_prb_candidate"], 5.21)
        self.assertEqual(event["avg_phr_candidate"], 16.08)
        self.assertEqual(event["pcmax_dbm_candidate"], 23.0)

    def test_nr_mac_pdsch_stats_v22_layout(self):
        header = bytearray(28)
        struct.pack_into("<HH", header, 0, 2, 2)
        header[15] = 1
        rec = bytearray(72)
        struct.pack_into("<IIIIIIIIQQQQQ", rec, 0, 0, 5490, 6, 5, 1, 2, 0, 0, 712, 100, 812, 429, 200)
        pkt = DiagLogPacket(0xB888, parse_qxdm_ts(0), 0, bytes(header + rec))
        event = MetricsParser().parse(pkt)[0]
        self.assertEqual(event["event"], "nr_mac_pdsch_stats")
        self.assertEqual(event["confidence"], "layout_v2_2_decoded")
        self.assertEqual(event["carrier_id"], 0)
        self.assertEqual(event["num_crc_pass_tb"], 5)
        self.assertEqual(event["num_crc_fail_tb"], 1)
        self.assertEqual(event["dl_bler"], 0.1667)
        self.assertEqual(event["retx_ratio"], 0.3333)
        self.assertEqual(event["byte_error_ratio"], 0.1232)


if __name__ == "__main__":
    unittest.main()
