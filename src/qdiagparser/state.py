from __future__ import annotations

import copy
import json
import pathlib
import time
from typing import Any


MISSING_METRIC_PLACEHOLDERS: dict[str, Any] = {
    "lte": {
        "dl_mcs": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB173 v36"]},
        "ul_mcs": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB139/0xB174"]},
        "dl_modulation": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB173 v36"]},
        "ul_modulation": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB139/0xB174"]},
        "dl_rb_alloc": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB173 v36"]},
        "ul_rb_alloc": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB139/0xB174"]},
        "cqi": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB14D/0xB14E"]},
        "ri": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB14D/0xB14E"]},
        "pmi": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB14D/0xB14E"]},
        "bler": {"value": None, "status": "not_decoded", "source_candidates": ["LTE 0xB173 v36"]},
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

    @staticmethod
    def _carrier_key(event: dict[str, Any]) -> str:
        for key in ("cc_id", "cell_id", "carrier_id"):
            if event.get(key) is not None:
                return str(event[key])
        return "unknown"

    @staticmethod
    def _cell_key(event: dict[str, Any]) -> str:
        earfcn = event.get("earfcn") or event.get("dl_earfcn") or event.get("nr_arfcn") or event.get("dl_nr_arfcn")
        pci = event.get("pci") or event.get("serving_pci")
        cell_index = event.get("cell_index")
        parts = [
            f"earfcn={earfcn}" if earfcn is not None else None,
            f"pci={pci}" if pci is not None else None,
            f"cell={cell_index}" if cell_index is not None else None,
        ]
        return "|".join(part for part in parts if part) or "unknown"

    def _refresh_lte_ca_observed(self) -> None:
        by_cc = self.latest["lte"].get("mac", {}).get("by_cc", {})
        cc_ids = sorted((key for key in by_cc.keys() if key != "unknown"), key=lambda value: int(value) if value.isdigit() else value)
        self.latest["lte"].setdefault("ca", {})["observed_cc_ids"] = cc_ids
        self.latest["lte"]["ca"]["observed_component_count"] = len(cc_ids)

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
                    mac = self.latest["lte"].setdefault("mac", {})
                    mac[direction] = event
                    cc_key = self._carrier_key(event)
                    mac.setdefault("by_cc", {}).setdefault(cc_key, {})[direction] = event
                    self._refresh_lte_ca_observed()
            elif name == "lte_phy_pusch_tx_candidate":
                self.latest["lte"].setdefault("phy", {})["pusch_tx"] = event
            elif name == "lte_phy_pdsch_stat_candidate":
                self.latest["lte"].setdefault("phy", {})["pdsch_stat"] = event
            elif name == "per_antenna_measurement":
                key = self._cell_key(event)
                self.latest["lte"].setdefault("per_antenna", {})[key] = event
            elif name == "serving_cell_info":
                key = self._cell_key(event)
                if rat == "lte":
                    self.latest["lte"].setdefault("cells", {})[key] = event
                elif rat == "nr":
                    self.latest["nr"].setdefault("cells", {})[key] = event
            elif name == "measurement_database_update":
                self.latest["nr"]["ml1_latest"] = event
                for layer in event.get("layers", []):
                    cc_id = layer.get("cc_id")
                    if cc_id is not None:
                        self.latest["nr"].setdefault("layers_by_cc", {})[str(cc_id)] = layer

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
