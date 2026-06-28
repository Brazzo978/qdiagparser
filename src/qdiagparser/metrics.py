from __future__ import annotations

import binascii
import struct
from dataclasses import dataclass
from typing import Any

from .bitutil import bits, bitstream_lsb_words
from .diag import DiagLogPacket
from .logcodes import NAMES


PRB_TO_MHZ = {6: 1.4, 15: 3, 25: 5, 50: 10, 75: 15, 100: 20}


@dataclass
class ParseWarning:
    log_id: int
    name: str
    reason: str
    raw_hex: str


def rsrp(raw: int) -> float:
    return round(-180 + raw * 0.0625, 2)


def rsrq(raw: int) -> float:
    return round(-30 + raw * 0.0625, 2)


def rssi(raw: int) -> float:
    return round(-110 + raw * 0.0625, 2)


def q7_signed(raw: int) -> float:
    if raw == 0:
        return 0.0
    integer = (raw >> 7) & 0xFF
    frac = raw & 0x7F
    return round((((integer ^ 0xFF) + 1) * -1) + frac * 0.0078125, 2)


def snr_lte(raw: int) -> float:
    return round(raw * 0.1 - 20.0, 2)


def ts(pkt: DiagLogPacket) -> str:
    return pkt.timestamp.isoformat()


def base(pkt: DiagLogPacket, event: str, rat: str) -> dict[str, Any]:
    return {
        "time": ts(pkt),
        "rat": rat,
        "event": event,
        "log_id": f"0x{pkt.log_id:04X}",
        "log_name": NAMES.get(pkt.log_id, "unknown"),
    }


def warn(pkt: DiagLogPacket, reason: str) -> dict[str, Any]:
    return {
        "time": ts(pkt),
        "event": "parse_warning",
        "log_id": f"0x{pkt.log_id:04X}",
        "log_name": NAMES.get(pkt.log_id, "unknown"),
        "reason": reason,
        "raw_hex": binascii.hexlify(pkt.body[:160]).decode(),
    }


class MetricsParser:
    def parse(self, pkt: DiagLogPacket) -> list[dict[str, Any]]:
        parser = {
            0xB063: self.lte_mac_dl_transport_block,
            0xB064: self.lte_mac_ul_transport_block,
            0xB139: self.lte_phy_pusch_tx_candidate,
            0xB173: self.lte_phy_pdsch_stat_candidate,
            0xB17F: self.lte_ml1_serving_cell_meas,
            0xB180: self.lte_ml1_neighbor_measurements,
            0xB193: self.lte_ml1_serving_cell_meas_response,
            0xB197: self.lte_ml1_serving_cell_info,
            0xB0C1: self.lte_rrc_mib,
            0xB0C2: self.lte_rrc_serving_cell_info,
            0xB0CD: self.lte_ca_combos_raw,
            0xB822: self.nr_rrc_mib_info,
            0xB823: self.nr_rrc_serving_cell_info,
            0xB826: self.nr_ca_combos_raw,
            0xB97F: self.nr_ml1_meas_database_update,
        }.get(pkt.log_id)
        if not parser:
            return []
        try:
            out = parser(pkt)
        except (IndexError, struct.error, ValueError) as exc:
            return [warn(pkt, str(exc))]
        return [out] if isinstance(out, dict) else out

    @staticmethod
    def _subpacket_body(body: bytes, pos: int) -> tuple[int, int, bytes, int]:
        sub_id, sub_ver, sub_size = struct.unpack("<BBH", body[pos:pos + 4])
        if sub_size >= 4 and pos + sub_size <= len(body):
            return sub_id, sub_ver, body[pos + 4:pos + sub_size], pos + sub_size
        return sub_id, sub_ver, body[pos + 4:pos + 4 + sub_size], pos + 4 + sub_size

    def lte_mac_dl_transport_block(self, pkt: DiagLogPacket) -> list[dict[str, Any]]:
        body = pkt.body
        if not body:
            return [warn(pkt, "empty LTE MAC DL transport block")]
        version = body[0]
        if version == 1:
            return self._lte_mac_v1_transport_blocks(pkt, True)
        if version in (0x31, 0x32):
            return self._lte_mac_dl_v49_transport_blocks(pkt)
        return [warn(pkt, f"unknown LTE MAC DL transport-block version {version}")]

    def lte_mac_ul_transport_block(self, pkt: DiagLogPacket) -> list[dict[str, Any]]:
        body = pkt.body
        if not body:
            return [warn(pkt, "empty LTE MAC UL transport block")]
        version = body[0]
        if version == 1:
            return self._lte_mac_v1_transport_blocks(pkt, False)
        return [warn(pkt, f"unknown LTE MAC UL transport-block version {version}")]

    def lte_phy_pusch_tx_candidate(self, pkt: DiagLogPacket) -> list[dict[str, Any]]:
        body = pkt.body
        if len(body) < 108 or body[0] != 0xA1:
            return [warn(pkt, "unknown LTE PHY PUSCH TX candidate layout")]
        if (len(body) - 8) % 100:
            return [warn(pkt, f"unexpected LTE PHY PUSCH TX candidate length {len(body)}")]
        header_tti = struct.unpack_from("<H", body, 4)[0]
        events: list[dict[str, Any]] = []
        for index, pos in enumerate(range(8, len(body), 100)):
            rec = body[pos:pos + 100]
            tti = struct.unpack_from("<H", rec, 0)[0]
            grant = struct.unpack_from("<H", rec, 8)[0]
            tx_power_raw = struct.unpack_from("<h", rec, 4)[0]
            event = base(pkt, "lte_phy_pusch_tx_candidate", "LTE")
            event.update({
                "version": body[0],
                "subversion": body[1],
                "header_tti": header_tti,
                "record_index": index,
                "record_count": (len(body) - 8) // 100,
                "tti": tti,
                "sfn_guess": (tti // 10) % 1024,
                "subframe_guess": tti % 10,
                "record_flags_raw": struct.unpack_from("<H", rec, 2)[0],
                "grant": grant,
                "tx_power_raw": tx_power_raw,
                "field_10_raw": struct.unpack_from("<H", rec, 10)[0],
                "field_34_raw": struct.unpack_from("<H", rec, 34)[0],
                "field_36_raw": struct.unpack_from("<H", rec, 36)[0],
                "field_40_raw": struct.unpack_from("<H", rec, 40)[0],
                "field_42_raw": struct.unpack_from("<H", rec, 42)[0],
                "field_46_raw": struct.unpack_from("<H", rec, 46)[0],
                "record_hex": rec.hex(),
            })
            events.append(event)
        return events

    def lte_phy_pdsch_stat_candidate(self, pkt: DiagLogPacket) -> list[dict[str, Any]]:
        body = pkt.body
        if len(body) < 44 or (len(body) - 4) % 40:
            return [warn(pkt, f"unexpected LTE PHY PDSCH stat candidate length {len(body)}")]
        header_word = struct.unpack_from("<H", body, 2)[0]
        events: list[dict[str, Any]] = []
        for index, pos in enumerate(range(4, len(body), 40)):
            rec = body[pos:pos + 40]
            event = base(pkt, "lte_phy_pdsch_stat_candidate", "LTE")
            event.update({
                "version": body[0],
                "subversion": body[1],
                "header_word": header_word,
                "record_index": index,
                "record_count": (len(body) - 4) // 40,
                "tti_guess": struct.unpack_from("<H", rec, 0)[0],
                "mcs_candidate_raw": rec[16],
                "field_0_raw": struct.unpack_from("<H", rec, 0)[0],
                "field_2_raw": struct.unpack_from("<H", rec, 2)[0],
                "field_4_raw": struct.unpack_from("<H", rec, 4)[0],
                "field_8_raw": struct.unpack_from("<H", rec, 8)[0],
                "field_12_raw": struct.unpack_from("<H", rec, 12)[0],
                "field_16_raw": struct.unpack_from("<H", rec, 16)[0],
                "field_18_raw": struct.unpack_from("<H", rec, 18)[0],
                "field_20_raw": struct.unpack_from("<H", rec, 20)[0],
                "field_24_raw": struct.unpack_from("<H", rec, 24)[0],
                "field_28_raw": struct.unpack_from("<H", rec, 28)[0],
                "confidence": "candidate_unconfirmed",
                "record_hex": rec.hex(),
            })
            events.append(event)
        return events

    def _lte_mac_v1_transport_blocks(self, pkt: DiagLogPacket, downlink: bool) -> list[dict[str, Any]]:
        body = pkt.body
        if len(body) < 8:
            return [warn(pkt, "short LTE MAC v1 body")]
        out: list[dict[str, Any]] = []
        pos = 4
        for _ in range(body[1]):
            if pos + 4 > len(body):
                break
            sub_id, sub_ver, sub, pos = self._subpacket_body(body, pos)
            if downlink and sub_id == 0x07:
                out.extend(self._lte_mac_v1_dl_subpacket(pkt, sub_ver, sub))
            elif not downlink and sub_id == 0x08:
                out.extend(self._lte_mac_v1_ul_subpacket(pkt, sub_ver, sub))
        return out or [warn(pkt, "no LTE MAC transport blocks decoded")]

    def _lte_mac_base(self, pkt: DiagLogPacket, direction: str, version: int) -> dict[str, Any]:
        event = base(pkt, "lte_mac_transport_block", "LTE")
        event.update({"version": version, "direction": direction})
        return event

    def _lte_mac_v1_dl_subpacket(self, pkt: DiagLogPacket, sub_ver: int, sub: bytes) -> list[dict[str, Any]]:
        out: list[dict[str, Any]] = []
        if not sub:
            return out
        pos = 1
        for sample_index in range(sub[0]):
            if sub_ver == 2:
                if pos + 12 > len(sub):
                    break
                sfn_subfn, rnti_type, harq_id, pmch_id, dl_tbs, rlc_pdus, padding, header_len = struct.unpack("<HBBHHBHB", sub[pos:pos + 12])
                cell_id = cc_id = None
                pos += 12
            elif sub_ver == 4:
                if pos + 14 > len(sub):
                    break
                _subid, cell_id, sfn_subfn, rnti_type, harq_id, pmch_id, dl_tbs, rlc_pdus, padding, header_len = struct.unpack("<BBHBBHHBHB", sub[pos:pos + 14])
                cc_id = cell_id
                pos += 14
            else:
                out.append(warn(pkt, f"unknown LTE MAC DL subpacket version {sub_ver}"))
                break
            mac_header = sub[pos:pos + header_len]
            pos += header_len
            event = self._lte_mac_base(pkt, "DL", sub_ver)
            event.update({
                "sample_index": sample_index,
                "sfn": sfn_subfn >> 4,
                "subframe": sfn_subfn & 0xF,
                "rnti_type": rnti_type,
                "harq_id": harq_id,
                "pmch_id": pmch_id,
                "tbs_bytes": dl_tbs,
                "rlc_pdus": rlc_pdus,
                "padding_bytes": padding,
                "mac_header_len": header_len,
                "mac_header_hex": mac_header.hex(),
            })
            if cell_id is not None:
                event["cell_id"] = cell_id
                event["cc_id"] = cc_id
            out.append(event)
        return out

    def _lte_mac_v1_ul_subpacket(self, pkt: DiagLogPacket, sub_ver: int, sub: bytes) -> list[dict[str, Any]]:
        out: list[dict[str, Any]] = []
        if not sub:
            return out
        pos = 1
        for sample_index in range(sub[0]):
            if sub_ver == 1:
                if pos + 12 > len(sub):
                    break
                harq_id, rnti_type, sfn_subfn, grant, rlc_pdus, padding, bsr_event, bsr_trig, header_len = struct.unpack("<BBHHBHBBB", sub[pos:pos + 12])
                subid = cell_id = None
                pos += 12
            elif sub_ver in (2, 3, 5, 8):
                if pos + 14 > len(sub):
                    break
                subid, cell_id, harq_id, rnti_type, sfn_subfn, grant, rlc_pdus, padding, bsr_event, bsr_trig, header_len = struct.unpack("<BBBBHHBHBBB", sub[pos:pos + 14])
                pos += 14
            else:
                out.append(warn(pkt, f"unknown LTE MAC UL subpacket version {sub_ver}"))
                break
            mac_header = sub[pos:pos + header_len]
            pos += header_len
            event = self._lte_mac_base(pkt, "UL", sub_ver)
            event.update({
                "sample_index": sample_index,
                "sfn": sfn_subfn >> 4,
                "subframe": sfn_subfn & 0xF,
                "rnti_type": rnti_type,
                "harq_id": harq_id,
                "grant": grant,
                "rlc_pdus": rlc_pdus,
                "padding_bytes": padding,
                "bsr_event": bsr_event,
                "bsr_trigger": bsr_trig,
                "mac_header_len": header_len,
                "mac_header_hex": mac_header.hex(),
            })
            if subid is not None:
                event["subid"] = subid
                event["cell_id"] = cell_id
            out.append(event)
        return out

    def _lte_mac_dl_v49_transport_blocks(self, pkt: DiagLogPacket) -> list[dict[str, Any]]:
        body = pkt.body
        if len(body) < 8:
            return [warn(pkt, "short LTE MAC DL v49 body")]
        version = body[0]
        num_tb, num_lcid, reason = struct.unpack("<HBB", body[4:8])
        pos = 8 + (19 * 28 if version == 0x31 else 0)
        out: list[dict[str, Any]] = []
        for tb_index in range(num_tb):
            if pos + 16 > len(body):
                break
            size, pad, sfn_word, cc_harq, num_sdu, hdr_len_word = struct.unpack("<LLLBBH", body[pos:pos + 16])
            pos += 16
            event = self._lte_mac_base(pkt, "DL", version)
            event.update({
                "tb_index": tb_index,
                "reason": reason,
                "num_lcid": num_lcid,
                "sfn": bits(sfn_word, 0, 10),
                "subframe": bits(sfn_word, 10, 4),
                "rnti_type": bits(sfn_word, 15, 4),
                "cc_id": bits(cc_harq, 0, 4),
                "harq_id": bits(cc_harq, 4, 4),
                "tbs_bytes": size,
                "padding_bytes": pad,
                "num_sdu": num_sdu,
                "mac_header_len": bits(hdr_len_word, 0, 12),
                "sdus": [],
            })
            for _ in range(num_sdu):
                if pos + 12 > len(body):
                    break
                common = int.from_bytes(body[pos:pos + 3], "little")
                pos += 3
                is_mce = bits(common, 0, 1)
                lcid = bits(common, 1, 6)
                sdu_len = bits(common, 7, 16)
                info = body[pos:pos + 9]
                pos += 9
                event["sdus"].append({"lcid": lcid, "sdu_len": sdu_len, "is_mce": bool(is_mce)})
                if not is_mce and len(info) >= 9:
                    num_pdcp_grp, num_dyn_log_info = struct.unpack("<5xBHx", info)
                    pos += num_dyn_log_info * 4
                    for _grp in range(num_pdcp_grp):
                        has_more = 1
                        while has_more and pos + 4 <= len(body):
                            grp = int.from_bytes(body[pos:pos + 4], "little")
                            has_more = bits(grp, 0, 1)
                            pos += 4
            out.append(event)
        return out or [warn(pkt, "no LTE MAC DL v49 transport blocks decoded")]

    def lte_ml1_serving_cell_meas(self, pkt: DiagLogPacket) -> dict[str, Any]:
        body = pkt.body
        version = body[0]
        if version == 4:
            rrc_rel, _r1, earfcn, pci_prio, meas_rsrp, avg_rsrp, rsrq_word, rssi_word, rxlev, s_search = struct.unpack("<BHHHLLLLLL", body[1:32])
        elif version == 5:
            rrc_rel, _r1, earfcn, pci_prio, meas_rsrp, avg_rsrp, rsrq_word, rssi_word, rxlev, s_search = struct.unpack("<BHLH2xLLLLLL", body[1:36])
        else:
            return warn(pkt, f"unknown LTE ML1 serving-cell version {version}")

        event = base(pkt, "serving_cell_measurement", "LTE")
        event.update({
            "version": version,
            "rrc_release": rrc_rel,
            "earfcn": earfcn,
            "pci": bits(pci_prio, 0, 9),
            "serving_layer_priority": bits(pci_prio, 9, 7),
            "rsrp_dbm": rsrp(meas_rsrp & 0xFFF),
            "avg_rsrp_dbm": rsrp(avg_rsrp & 0xFFF),
            "rssi_dbm": rssi(bits(rssi_word, 10, 11)),
            "rsrq_db": rsrq(bits(rsrq_word, 0, 10)),
            "avg_rsrq_db": rsrq(bits(rsrq_word, 20, 10)),
            "q_rxlevmin": bits(rxlev, 0, 6),
            "p_max": bits(rxlev, 6, 7),
            "max_ue_tx_power": bits(rxlev, 13, 6),
            "s_rxlev": bits(rxlev, 19, 7),
            "s_intra_search": bits(s_search, 0, 6),
            "s_non_intra_search": bits(s_search, 6, 6),
        })
        return event

    def lte_ml1_neighbor_measurements(self, pkt: DiagLogPacket) -> dict[str, Any]:
        body = pkt.body
        version = body[0]
        if version == 4:
            rrc_rel, _r1, earfcn, qrx_ncells = struct.unpack("<BHHH", body[1:8])
            pos = 8
        elif version == 5:
            rrc_rel, _r1, earfcn, qrx_ncells = struct.unpack("<BHLL", body[1:12])
            pos = 12
        else:
            return warn(pkt, f"unknown LTE ML1 neighbor version {version}")

        event = base(pkt, "neighbor_measurements", "LTE")
        cells = []
        n_cells = qrx_ncells >> 6
        for idx in range(n_cells):
            cell = body[pos + idx * 32:pos + (idx + 1) * 32]
            if len(cell) < 28:
                break
            val0, val1, val2, val3, freq_offset, _v5, ant0, ant1 = struct.unpack("<LLLLHHLL", cell[:28])
            cells.append({
                "index": idx,
                "pci": bits(val0, 0, 9),
                "rsrp_dbm": rsrp(bits(val0, 20, 12)),
                "avg_rsrp_dbm": rsrp(bits(val1, 12, 12)),
                "rssi_dbm": rssi(bits(val0, 9, 11)),
                "rsrq_db": rsrq(bits(val2, 12, 10)),
                "avg_rsrq_db": rsrq(bits(val3, 0, 10)),
                "s_rxlev": bits(val3, 20, 6),
                "freq_offset": freq_offset,
                "ant0_frame_offset": bits(ant0, 0, 11),
                "ant0_sample_offset": ant0 >> 11,
                "ant1_frame_offset": bits(ant1, 0, 11),
                "ant1_sample_offset": ant1 >> 11,
            })
        event.update({"version": version, "rrc_release": rrc_rel, "earfcn": earfcn, "q_rxlevmin": bits(qrx_ncells, 0, 6), "cells": cells})
        return event

    def lte_ml1_serving_cell_meas_response(self, pkt: DiagLogPacket) -> list[dict[str, Any]]:
        body = pkt.body
        if body[0] != 1:
            return [warn(pkt, f"unknown LTE ML1 serving-cell response version {body[0]}")]
        out = []
        num_subpkts = body[1]
        pos = 4
        for _ in range(num_subpkts):
            sub_id, sub_ver, sub_size = struct.unpack("<BBH", body[pos:pos + 4])
            sub = body[pos + 4:pos + sub_size]
            pos += sub_size
            if sub_id != 0x19:
                continue
            if sub_ver == 36:
                earfcn, num_cells, valid_rx = struct.unpack("<LHH", sub[:8])
                cell_size, cell_pos, snr_offset, cinr_offset = 128, 8, 80, 104
            elif sub_ver in (48, 50):
                earfcn, num_cells, valid_rx, rx_map = struct.unpack("<LHHL", sub[:12])
                cell_size, cell_pos, snr_offset, cinr_offset = 140, 12, 92, 116
            elif sub_ver == 59:
                earfcn, num_cells, valid_rx, rx_map = struct.unpack("<LLLL", sub[:16])
                cell_size, cell_pos, snr_offset, cinr_offset = 156, 16, 92, 116
                for idx in range(num_cells):
                    cell = sub[cell_pos + idx * cell_size:cell_pos + (idx + 1) * cell_size]
                    if len(cell) < 32:
                        continue
                    out.append(self._lte_meas_response_cell_v59(pkt, sub_ver, earfcn, valid_rx, rx_map, idx, cell))
                continue
            else:
                out.append(warn(pkt, f"unknown LTE ML1 serving-cell response subpacket version {sub_ver}"))
                continue
            for idx in range(num_cells):
                cell = sub[cell_pos + idx * cell_size:cell_pos + (idx + 1) * cell_size]
                if len(cell) < cinr_offset + 24:
                    continue
                out.append(self._lte_meas_response_cell(pkt, sub_ver, earfcn, valid_rx, idx, cell, snr_offset, cinr_offset))
        return out

    def _lte_meas_response_cell(self, pkt: DiagLogPacket, sub_ver: int, earfcn: int, valid_rx: int, idx: int, cell: bytes, snr_offset: int, cinr_offset: int) -> dict[str, Any]:
        meta0, _meta1, meta2 = struct.unpack("<HHH", cell[:6])
        words = list(struct.unpack("<LLLLLLLLLLLL", cell[16:64]))
        packed = bitstream_lsb_words(words)
        snr_words = list(struct.unpack("<LL", cell[snr_offset:snr_offset + 8]))
        snr_packed = bitstream_lsb_words(snr_words)
        prj_sir, post_ic_rsrq, cinr0, cinr1, cinr2, cinr3 = struct.unpack("<LLllll", cell[cinr_offset:cinr_offset + 24])
        if prj_sir & (1 << 31):
            prj_sir -= 1 << 32

        event = base(pkt, "per_antenna_measurement", "LTE")
        event.update({
            "version": sub_ver,
            "earfcn": earfcn,
            "cell_index": idx,
            "valid_rx": valid_rx,
            "pci": bits(meta0, 0, 9),
            "serving_cell_index": bits(meta0, 9, 3),
            "is_serving_cell": bool(bits(meta0, 12, 1)),
            "sfn": bits(meta2, 0, 10),
            "subframe": bits(meta2, 10, 4),
            "rsrp_dbm": [rsrp(bits(packed, x, 12)) for x in (10, 44, 76, 96)],
            "combined_rsrp_dbm": round(rsrp(bits(packed, 108, 12)) + 40, 2),
            "filtered_rsrp_dbm": rsrp(bits(packed, 140, 12)),
            "rsrq_db": [rsrq(bits(packed, x, 10)) for x in (160, 180, 202, 212)],
            "combined_rsrq_db": rsrq(bits(packed, 224, 10)),
            "filtered_rsrq_db": rsrq(bits(packed, 244, 10)),
            "rssi_dbm": [rssi(bits(packed, x, 11)) for x in (256, 267, 288, 299)],
            "combined_rssi_dbm": rssi(bits(packed, 320, 11)),
            "snr_db": [snr_lte(bits(snr_packed, x, 9)) for x in (0, 9, 32, 42)],
            "projected_sir_db": round(prj_sir / 16.0, 2),
            "post_ic_rsrq_db": round(post_ic_rsrq * 0.0625 - 30.0, 2),
            "cinr_raw": [cinr0, cinr1, cinr2, cinr3],
        })
        return event

    def _lte_meas_response_cell_v59(self, pkt: DiagLogPacket, sub_ver: int, earfcn: int, valid_rx: int, rx_map: int, idx: int, cell: bytes) -> dict[str, Any]:
        meta0, meta1, meta2 = struct.unpack("<HHH", cell[:6])
        event = base(pkt, "per_antenna_measurement_raw", "LTE")
        event.update({
            "version": sub_ver,
            "earfcn": earfcn,
            "cell_index": idx,
            "valid_rx": valid_rx,
            "rx_map": rx_map,
            "pci_guess": bits(meta0, 0, 9),
            "serving_cell_index_guess": bits(meta0, 9, 3),
            "is_serving_cell_guess": bool(bits(meta0, 12, 1)),
            "meta_words": [meta0, meta1, meta2],
            "cell_hex": cell.hex(),
        })
        return event

    def lte_ml1_serving_cell_info(self, pkt: DiagLogPacket) -> dict[str, Any]:
        body = pkt.body
        version = body[0]
        if version == 1:
            dl_bw, sfn, earfcn, pci_word, _pss, _sss, ref_time, mib_bytes, freq_offset, num_ant = struct.unpack("<BHHHLLQLhH", body[1:32])
        elif version == 2:
            dl_bw, sfn, earfcn, pci_word, _pss, _sss, ref_time, mib_bytes, freq_offset, num_ant = struct.unpack("<BHLLLLQLhH", body[1:36])
        else:
            return warn(pkt, f"unknown LTE ML1 cell-info version {version}")
        event = base(pkt, "cell_info", "LTE")
        event.update({
            "version": version,
            "earfcn": earfcn,
            "pci": bits(pci_word & 0xFFFF, 0, 9),
            "sfn": sfn,
            "bandwidth_prb": dl_bw,
            "bandwidth_mhz": PRB_TO_MHZ.get(dl_bw),
            "num_antennas": num_ant,
            "freq_offset": freq_offset,
            "ref_time": ref_time,
            "mib_hex": f"{mib_bytes:08x}"[:6],
        })
        return event

    def lte_rrc_mib(self, pkt: DiagLogPacket) -> dict[str, Any]:
        body = pkt.body
        version = body[0]
        if version == 1:
            pci, earfcn, sfn, tx_ant, bw = struct.unpack("<HHHBB", body[1:10])
        elif version == 2:
            pci, earfcn, sfn, tx_ant, bw = struct.unpack("<HLHBB", body[1:12])
        elif version == 17:
            pci, earfcn, sfn, *_rest, tx_ant = struct.unpack("<HLHBBBBBBHB", body[1:18])
            bw = 1
        else:
            return warn(pkt, f"unknown LTE MIB version {version}")
        event = base(pkt, "mib", "LTE")
        event.update({"version": version, "pci": pci, "earfcn": earfcn, "sfn": sfn, "tx_antennas": tx_ant, "bandwidth_prb": bw, "bandwidth_mhz": PRB_TO_MHZ.get(bw)})
        return event

    def lte_rrc_serving_cell_info(self, pkt: DiagLogPacket) -> dict[str, Any]:
        body = pkt.body
        version = body[0]
        if version == 2:
            pci, dl_earfcn, ul_earfcn, dl_bw, ul_bw, cell_id, tac, band, mcc, mnc_digit, mnc, allowed = struct.unpack("<HHHBBLHLHBHB", body[1:25])
        elif version == 3:
            pci, dl_earfcn, ul_earfcn, dl_bw, ul_bw, cell_id, tac, band, mcc, mnc_digit, mnc, allowed = struct.unpack("<HLLBBLHLHBHB", body[1:29])
        else:
            return warn(pkt, f"unknown LTE RRC serving-cell version {version}")
        event = base(pkt, "serving_cell_info", "LTE")
        event.update({
            "version": version,
            "pci": pci,
            "dl_earfcn": dl_earfcn,
            "ul_earfcn": ul_earfcn,
            "band": band,
            "dl_bandwidth_prb": dl_bw,
            "ul_bandwidth_prb": ul_bw,
            "dl_bandwidth_mhz": PRB_TO_MHZ.get(dl_bw),
            "ul_bandwidth_mhz": PRB_TO_MHZ.get(ul_bw),
            "cell_id": cell_id,
            "tac": tac,
            "mcc": mcc,
            "mnc": f"{mnc:0{mnc_digit}d}" if mnc_digit in (2, 3) else str(mnc),
            "allowed_access": allowed,
        })
        return event

    def lte_ca_combos_raw(self, pkt: DiagLogPacket) -> dict[str, Any]:
        event = base(pkt, "supported_ca_combos_raw", "LTE")
        event.update({"format": "QLTE", "payload_hex": binascii.hexlify(pkt.body).decode()})
        return event

    def nr_ca_combos_raw(self, pkt: DiagLogPacket) -> dict[str, Any]:
        event = base(pkt, "supported_ca_combos_raw", "NR")
        event.update({"format": "QNR", "payload_hex": binascii.hexlify(pkt.body).decode()})
        return event

    def nr_rrc_mib_info(self, pkt: DiagLogPacket) -> dict[str, Any]:
        body = pkt.body
        rel_min, rel_maj = struct.unpack("<HH", body[:4])
        if rel_maj == 0 and rel_min == 3:
            pci, nrarfcn = struct.unpack("<HI", body[4:10])
            props = int.from_bytes(body[10:14], "little")
            sfn, scs = bits(props, 0, 10), bits(props, 30, 2)
        elif rel_maj == 2 and rel_min == 0:
            pci, nrarfcn = struct.unpack("<HI", body[4:10])
            props = int.from_bytes(body[10:15], "little")
            sfn, scs = bits(props, 0, 10), bits(props, 31, 2)
        else:
            return warn(pkt, f"unknown NR MIB version {rel_maj}.{rel_min}")
        event = base(pkt, "mib", "NR")
        event.update({"version": f"{rel_maj}.{rel_min}", "pci": pci, "nr_arfcn": nrarfcn, "sfn": sfn, "scs_khz": {0: 15, 1: 30, 2: 60, 3: 120}.get(scs)})
        return event

    def nr_rrc_serving_cell_info(self, pkt: DiagLogPacket) -> dict[str, Any]:
        body = pkt.body
        rel_min, rel_maj = struct.unpack("<HH", body[:4])
        if rel_maj == 0 and rel_min == 4:
            off = 4
        elif rel_maj == 3 and rel_min == 0:
            off = 4
        elif rel_maj == 3 and rel_min in (2, 3):
            off = 7
        else:
            return warn(pkt, f"unknown NR RRC serving-cell version {rel_maj}.{rel_min}")
        if rel_maj == 0:
            pci, dl, ul, dl_bw, ul_bw, cell_id, mcc, mnc_digit, mnc, allowed, tac, band = struct.unpack("<HLLHHQHBHBLH", body[off:off + 34])
        else:
            pci, ncgi, dl, ul, dl_bw, ul_bw, cell_id, mcc, mnc_digit, mnc, allowed, tac, band = struct.unpack("<HQLLHHQHBHBLH", body[off:off + 42])
        event = base(pkt, "serving_cell_info", "NR")
        event.update({
            "version": f"{rel_maj}.{rel_min}",
            "pci": pci,
            "dl_nr_arfcn": dl,
            "ul_nr_arfcn": ul,
            "band": band,
            "dl_bandwidth_mhz": dl_bw,
            "ul_bandwidth_mhz": ul_bw,
            "cell_id": cell_id,
            "tac": tac,
            "mcc": mcc,
            "mnc": f"{mnc:0{mnc_digit}d}" if mnc_digit in (2, 3) else str(mnc),
            "allowed_access": allowed,
        })
        if rel_maj != 0:
            event["nr_cgi"] = ncgi
        return event

    def nr_ml1_meas_database_update(self, pkt: DiagLogPacket) -> dict[str, Any]:
        body = pkt.body
        rel_min, rel_maj = struct.unpack("<HH", body[:4])
        if rel_maj == 2 and rel_min == 7:
            num_layers = body[4]
            ssb_periodicity = body[5]
            pos = 16
        elif rel_maj == 2 and rel_min in (9, 10):
            _unk, num_layers, ssb_periodicity, _null, _fo, _to = struct.unpack("<IBBHII", body[4:20])
            pos = 20
        elif rel_maj == 3 and rel_min == 0:
            _unk, num_layers, ssb_periodicity, _null, _fo, _to = struct.unpack("<IBBHII", body[4:20])
            pos = 20
        else:
            return warn(pkt, f"unknown NR ML1 measurement version {rel_maj}.{rel_min}")

        event = base(pkt, "measurement_database_update", "NR")
        layers = []
        for layer_index in range(num_layers):
            if rel_maj == 2:
                vals = struct.unpack("<IBBHB3sIIHHH2sHH", body[pos:pos + 32])
                pos += 32
                carrier = {
                    "layer": layer_index,
                    "nr_arfcn": vals[0],
                    "num_cells": vals[1],
                    "serving_cell_index": vals[2],
                    "serving_pci": vals[3],
                    "serving_ssb": vals[4] & 0xF,
                    "serving_rsrp_dbm": [q7_signed(vals[6]), q7_signed(vals[7])],
                    "rx_beam": [None if vals[8] == 0xFFFF else vals[8], None if vals[9] == 0xFFFF else vals[9]],
                    "rfic_id": vals[10],
                    "subarray": [vals[12], vals[13]],
                }
            else:
                vals = struct.unpack("<IBBHBB2sIIIIHHH2sHH", body[pos:pos + 40])
                pos += 40
                carrier = {
                    "layer": layer_index,
                    "nr_arfcn": vals[0],
                    "cc_id": vals[1],
                    "num_cells": vals[2],
                    "serving_pci": vals[3],
                    "serving_cell_index": vals[4],
                    "serving_ssb": vals[5] & 0xF,
                    "serving_rsrp_dbm": [q7_signed(vals[7]), q7_signed(vals[8]), q7_signed(vals[9]), q7_signed(vals[10])],
                    "rx_beam": [None if vals[11] == 0xFFFF else vals[11], None if vals[12] == 0xFFFF else vals[12]],
                    "rfic_id": vals[13],
                    "subarray": [vals[15], vals[16]],
                }

            n_cells = carrier["num_cells"]
            if n_cells in (0, 0xFF):
                n_cells = carrier["serving_cell_index"] if 0 < carrier["serving_cell_index"] < 0xFF else 0
            cells = []
            for cell_index in range(n_cells):
                pci, pbch_sfn, num_beams, _null, cell_rsrp, cell_rsrq = struct.unpack("<HHB3sII", body[pos:pos + 16])
                pos += 16
                cell = {
                    "index": cell_index,
                    "pci": pci,
                    "pbch_sfn": pbch_sfn,
                    "rsrp_dbm": q7_signed(cell_rsrp),
                    "rsrq_db": q7_signed(cell_rsrq),
                    "beams": [],
                }
                for beam_index in range(num_beams):
                    if rel_maj == 2 and rel_min in (7, 9):
                        beam = struct.unpack("<HHHHIQIIIIII", body[pos:pos + 44])
                        pos += 44
                        cell["beams"].append({
                            "index": beam_index,
                            "ssb_index": beam[0],
                            "rx_beam": [beam[2], beam[3]],
                            "ssb_ref_timing": beam[5],
                            "rsrp_dbm": [q7_signed(beam[6]), q7_signed(beam[7])],
                            "filtered_nr2nr_rsrp_dbm": q7_signed(beam[8]),
                            "filtered_nr2nr_rsrq_db": q7_signed(beam[9]),
                            "filtered_l2nr_rsrp_dbm": q7_signed(beam[10]),
                            "filtered_l2nr_rsrq_db": q7_signed(beam[11]),
                        })
                    else:
                        beam = struct.unpack("<HHHHIQIIIIIIIIIIIIIIII", body[pos:pos + 84])
                        pos += 84
                        cell["beams"].append({
                            "index": beam_index,
                            "ssb_index": beam[0],
                            "rx_beam": [beam[2], beam[3]],
                            "ssb_ref_timing": beam[5],
                            "rsrp_dbm": [q7_signed(beam[6]), q7_signed(beam[7])],
                            "rsrq_db": [q7_signed(beam[8]), q7_signed(beam[9])],
                            "filtered_nr2nr_rsrp_dbm": q7_signed(beam[18]),
                            "filtered_nr2nr_rsrq_db": q7_signed(beam[19]),
                            "filtered_l2nr_rsrp_dbm": q7_signed(beam[20]),
                            "filtered_l2nr_rsrq_db": q7_signed(beam[21]),
                        })
                cells.append(cell)
            carrier["cells"] = cells
            layers.append(carrier)
        event.update({"version": f"{rel_maj}.{rel_min}", "num_layers": num_layers, "ssb_periodicity": ssb_periodicity, "layers": layers})
        return event
