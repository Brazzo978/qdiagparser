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


if __name__ == "__main__":
    unittest.main()
