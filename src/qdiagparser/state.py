from __future__ import annotations

import copy
import json
import pathlib
import time
from typing import Any


MISSING_METRIC_PLACEHOLDERS: dict[str, Any] = {
    "lte": {
        "dl_mcs": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB130/0xB132/0xB144/0xB173"]},
        "ul_mcs": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB139/0xB174"]},
        "dl_modulation": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB130/0xB132/0xB144/0xB173"]},
        "ul_modulation": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB139/0xB174"]},
        "dl_rb_alloc": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB126/0xB130/0xB132"]},
        "ul_rb_alloc": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB139/0xB174"]},
        "cqi": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB140/0xB175/0xB176"]},
        "ri": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB177"]},
        "pmi": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB178"]},
        "bler": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB144/0xB173"]},
        "tx_power_dbm": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB139/0xB16D/0xB174"]},
    },
    "nr": {
        "nr_mode": {"value": None, "status": "not_decoded", "source_candidates": ["NR NAS/RRC state logs"]},
        "endc_anchor": {"value": None, "status": "not_decoded", "source_candidates": ["LTE/NR RRC configuration logs"]},
        "ss_rsrp_dbm": {"value": None, "status": "not_decoded", "source_candidates": ["NR ML1 measurement database"]},
        "ss_rsrq_db": {"value": None, "status": "not_decoded", "source_candidates": ["NR ML1 measurement database"]},
        "ss_sinr_db": {"value": None, "status": "not_decoded", "source_candidates": ["NR ML1 measurement/log version mapping"]},
        "per_rx_rsrp_dbm": {"value": None, "status": "not_decoded", "source_candidates": ["NR ML1 measurement database"]},
        "per_rx_sinr_db": {"value": None, "status": "not_decoded", "source_candidates": ["NR ML1 measurement/log version mapping"]},
        "dl_mcs": {"value": None, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]},
        "ul_mcs": {"value": None, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]},
        "dl_modulation": {"value": None, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]},
        "ul_modulation": {"value": None, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]},
        "dl_rb_alloc": {"value": None, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]},
        "ul_rb_alloc": {"value": None, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]},
        "rank_ri": {"value": None, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]},
        "cqi": {"value": None, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]},
        "tx_power_dbm": {"value": None, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]},
    },
}


class MetricsState:
    def __init__(self):
        self.started_at = time.time()
        self.updated_at = self.started_at
        self.events_seen = 0
        self.latest: dict[str, Any] = {
            "lte": {},
            "nr": {},
            "combos": {},
            "missing_metrics": copy.deepcopy(MISSING_METRIC_PLACEHOLDERS),
            "warnings": [],
        }

    def update(self, event: dict[str, Any]) -> None:
        self.events_seen += 1
        self.updated_at = time.time()
        rat = event.get("rat", "").lower()
        name = event.get("event")
        if name == "parse_warning":
            self.latest["warnings"] = (self.latest["warnings"] + [event])[-20:]
            return
        if name == "supported_ca_combos_raw":
            key = "lte" if rat == "lte" else "nr"
            self.latest["combos"][key] = event
            return
        if rat in ("lte", "nr") and name:
            self.latest[rat][name] = event
            if name == "lte_mac_transport_block":
                direction = str(event.get("direction", "")).lower()
                if direction in ("dl", "ul"):
                    self.latest["lte"].setdefault("mac", {})[direction] = event
            elif name == "lte_phy_pusch_tx_candidate":
                self.latest["lte"].setdefault("phy", {})["pusch_tx"] = event
            elif name == "per_antenna_measurement":
                key = str(event.get("cell_index", "unknown"))
                self.latest["lte"].setdefault("per_antenna", {})[key] = event
            elif name == "measurement_database_update":
                self.latest["nr"]["ml1_latest"] = event

    def snapshot(self) -> dict[str, Any]:
        data = copy.deepcopy(self.latest)
        data["meta"] = {
            "started_at": self.started_at,
            "updated_at": self.updated_at,
            "events_seen": self.events_seen,
        }
        return data

    def write(self, path: str) -> None:
        target = pathlib.Path(path)
        tmp = target.with_suffix(target.suffix + ".tmp")
        tmp.write_text(json.dumps(self.snapshot(), indent=2, sort_keys=True) + "\n")
        tmp.replace(target)
