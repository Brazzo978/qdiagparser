import unittest

from qdiagparser.state import MetricsState


class StateTests(unittest.TestCase):
    def test_gui_placeholders_and_mac_direction_state(self):
        state = MetricsState()
        state.update({
            "event": "lte_mac_transport_block",
            "rat": "LTE",
            "direction": "DL",
            "tbs_bytes": 1234,
        })
        snap = state.snapshot()
        self.assertIn("missing_metrics", snap)
        self.assertIsNone(snap["missing_metrics"]["lte"]["dl_mcs"]["value"])
        self.assertEqual(snap["missing_metrics"]["lte"]["dl_mcs"]["status"], "not_decoded")
        self.assertEqual(snap["lte"]["mac"]["dl"]["tbs_bytes"], 1234)

    def test_lte_mac_is_normalized_by_component_carrier(self):
        state = MetricsState()
        state.update({
            "event": "lte_mac_transport_block",
            "rat": "LTE",
            "direction": "DL",
            "cc_id": 0,
            "tbs_bytes": 100,
        })
        state.update({
            "event": "lte_mac_transport_block",
            "rat": "LTE",
            "direction": "DL",
            "cc_id": 3,
            "tbs_bytes": 400,
        })
        snap = state.snapshot()
        self.assertEqual(snap["lte"]["mac"]["by_cc"]["0"]["dl"]["tbs_bytes"], 100)
        self.assertEqual(snap["lte"]["mac"]["by_cc"]["3"]["dl"]["tbs_bytes"], 400)
        self.assertEqual(snap["lte"]["ca"]["observed_cc_ids"], ["0", "3"])
        self.assertEqual(snap["lte"]["ca"]["observed_component_count"], 2)

    def test_lte_per_antenna_is_normalized_by_cell_identity(self):
        state = MetricsState()
        state.update({
            "event": "per_antenna_measurement",
            "rat": "LTE",
            "earfcn": 100,
            "pci": 10,
            "cell_index": 0,
            "rsrp_dbm": [-90.0],
        })
        state.update({
            "event": "per_antenna_measurement",
            "rat": "LTE",
            "earfcn": 200,
            "pci": 20,
            "cell_index": 0,
            "rsrp_dbm": [-95.0],
        })
        snap = state.snapshot()
        self.assertEqual(len(snap["lte"]["per_antenna"]), 2)
        self.assertIn("earfcn=100|pci=10|cell=0", snap["lte"]["per_antenna"])
        self.assertIn("earfcn=200|pci=20|cell=0", snap["lte"]["per_antenna"])

    def test_lte_phy_candidates_are_grouped(self):
        state = MetricsState()
        state.update({
            "event": "lte_phy_pdsch_stat_candidate",
            "rat": "LTE",
            "mcs_candidate_raw": 18,
            "confidence": "candidate_unconfirmed",
        })
        snap = state.snapshot()
        self.assertEqual(snap["lte"]["phy"]["pdsch_stat"]["mcs_candidate_raw"], 18)
        self.assertEqual(snap["lte"]["phy"]["pdsch_stat"]["confidence"], "candidate_unconfirmed")


if __name__ == "__main__":
    unittest.main()
