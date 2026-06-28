# GUI Handoff

This file is the contract for a modem-side advanced signal GUI fed by `qdiagmon-dci`.

## Runtime

Preferred runtime on T99W175-style firmware:

```sh
/tmp/qdiagmon-dci \
  --snapshot-file /tmp/qdiag-state.json \
  --snapshot-interval-ms 10000 \
  --no-raw-log \
  --max-runtime-sec 600 \
  --mac \
  --combo-dir /tmp/qdiag-combos \
  >/dev/null 2>/tmp/qdiag-state.err &
```

This is the production WebUI path. The binary collects DIAG, updates an in-memory aggregate state, and atomically writes a small snapshot:

```text
/tmp/qdiag-state.json.tmp -> rename -> /tmp/qdiag-state.json
```

The CGI must not parse large JSONL logs. It should only return the snapshot:

```sh
#!/bin/sh
printf 'Content-Type: application/json\r\n\r\n'
cat /tmp/qdiag-state.json 2>/dev/null || \
  printf '{"updated_at":0,"stale_after_ms":30000,"lte":{"serving_cell":{},"per_antenna":[],"mac":{},"ca":{}},"nr":{"layers":[],"ca":{}},"runtime":{"running":false,"uptime_s":0,"events_seen":0,"last_error":"no snapshot yet"}}\n'
```

GUI polling defaults:

```json
{"poll_interval_ms":10000,"stale_after_ms":30000}
```

Do not delete `/tmp/qdiag-state.json` after a CGI call. The binary overwrites the same file; deleting it creates empty responses, races with the writer, and unnecessary flash/tmpfs churn.

The snapshot is intentionally compact and GUI-ready:

```json
{
  "updated_at": 1782651117,
  "stale_after_ms": 30000,
  "lte": {
    "serving_cell": {},
    "serving_info": {},
    "per_antenna": [],
    "mac": {
      "dl_by_cc": [],
      "ul_by_cc": [],
      "phy_pusch_tx": {}
    },
    "ca": {
      "observed_cc_ids": [],
      "observed_component_count": 0,
      "supported_combos": {}
    }
  },
  "nr": {
    "serving_cell": {},
    "layers": [],
    "ca": {
      "supported_combos": {}
    }
  },
  "runtime": {
    "running": true,
    "uptime_s": 120,
    "events_seen": 4520,
    "last_error": ""
  }
}
```

Fallback policy for old binaries without snapshot mode:

```sh
QDIAG_SECONDS=2 QDIAG_MAX_AGE=10 QDIAG_MAC=1 QDIAG_PROBE_PHY=0 \
  /tmp/qdiag-gui-sample.sh > /tmp/qdiag-gui-response.jsonl
```

This shell sampler runs a short one-shot capture only when the cache is older than 10 seconds. Prefer native snapshot mode whenever possible.

For parser development or offline evidence only, JSONL stream mode is still available:

```sh
/tmp/qdiagmon-dci --seconds 30 --mac --combo-dir /tmp/qdiag-combos > /tmp/qdiag-live.jsonl
```

For parser development only, probe extra scheduling candidates:

```sh
/tmp/qdiagmon-dci --seconds 30 --mac --probe-scheduling > /tmp/qdiag-probe.jsonl
/tmp/qdiagmon-dci --seconds 60 --mac --probe-phy > /tmp/qdiag-phy.jsonl
```

`--probe-scheduling` adds raw output for these candidate logs:

- LTE `0xB167`..`0xB16A`: ML1 MAC RAR/Msg1..Msg4 reports.
- LTE `0xB179`: connected-mode intra-frequency measurements.
- LTE `0xB192`: neighbor measurement request/response.
- LTE `0xB194`: search request/response.
- LTE `0xB195`: connected neighbor measurement request/response.
- NR `0xB88A`: NR MAC RACH attempt.

These are not guaranteed to contain MCS/RB/modulation. They are a safe next probe set found from SCAT/Qualcomm log names.

`--probe-phy` is the next, more targeted search mode. It enables LTE PHY candidate logs from the local Qualcomm/QXDM golden logmask: `0xB126`, `0xB130`, `0xB132`, `0xB139`, `0xB13C`, `0xB140`, `0xB144`, `0xB16B`, `0xB16D`, `0xB173`, `0xB174`, `0xB175`, `0xB176`, `0xB177`, and `0xB178`. Treat all output as raw evidence until a field offset is proven under load.

Keep `--probe-phy` out of the normal page refresh path. It should be exposed only as an explicit debug capture button because it produces many more events and can raise CPU load.

On a Python-capable host, use the Python state writer:

```sh
PYTHONPATH=src python3 -m qdiagparser parse capture.qmdl \
  --state-file latest-signals.json \
  --combo-dir ./qdiag-combos
```

The modem-side C runtime now writes the same kind of latest-state snapshot directly. Keep JSONL mode for development captures, not for the production CGI.

## Minimum Functional Dashboard

Render these first:

- LTE serving cell: `serving_cell_measurement`
- LTE per-antenna cell measurements: `per_antenna_measurement`
- LTE MAC DL/UL transport: `lte_mac_transport_block`
- LTE PHY candidate/debug: `lte_phy_pusch_tx_candidate` only in probe/debug views
- LTE serving info: `serving_cell_info`
- NR ML1 measurements: `measurement_database_update`
- NR serving info: `serving_cell_info` where `rat == "NR"`
- CA capability payloads: `supported_ca_combos_raw`
- Warnings/debug: `parse_warning`, `diag_log_raw` only in debug mode

## State Shape

Top-level keys:

- `lte`: latest LTE metrics and helper groups.
- `nr`: latest NR metrics and helper groups.
- `runtime`: process status, uptime, event count, and last error.
- `updated_at` and `stale_after_ms`: top-level freshness contract for the GUI.

The compact modem snapshot does not repeat `missing_metrics` on every write. Keep those placeholders static in the GUI from the list below and render them as unavailable until a decoder fills them.

Important nested keys:

- `lte.per_antenna[]`: latest LTE per-RX metrics keyed internally by `earfcn + pci + cell_index`.
- `lte.mac.dl_by_cc[]`: latest decoded LTE DL transport block per component carrier.
- `lte.mac.ul_by_cc[]`: latest decoded LTE UL transport block per component carrier.
- `lte.ca.observed_cc_ids[]`: component carriers observed from MAC activity, already normalized for 2CA/3CA/4CA views.
- `nr.serving_cell`: latest NR serving cell info when emitted by the modem.
- `nr.layers[]`: reserved for NR layer metrics; currently empty in the C snapshot until the NR ML1 decoder is promoted into the runtime.

## Stable Events

### `per_antenna_measurement`

Source: LTE `0xB193`.

Use for advanced LTE signal view:

- `earfcn`, `pci`, `cell_index`, `is_serving_cell`
- `valid_rx`, `rx_map`
- `rsrp_dbm[]`, `rsrq_db[]`, `rssi_dbm[]`, `snr_db[]`
- `combined_rsrp_dbm`, `combined_rsrq_db`, `combined_rssi_dbm`
- `filtered_rsrp_dbm`, `filtered_rsrq_db`
- `projected_sir_db`, `post_ic_rsrq_db`, `cinr_raw[]`

### `serving_cell_measurement`

Source: LTE `0xB17F`.

Use for headline LTE signal:

- `earfcn`, `pci`
- `rsrp_dbm`, `avg_rsrp_dbm`
- `rsrq_db`, `avg_rsrq_db`
- `rssi_dbm`

### `lte_mac_transport_block`

Source: LTE `0xB063`/`0xB064`.

Use for traffic/activity view:

- Common: `direction`, `sfn`, `subframe`, `harq_id`, `rnti_type`
- DL: `tbs_bytes`, `padding_bytes`, `cc_id`, `num_sdu`, `num_lcid`
- UL: `grant`, `rlc_pdus`, `padding_bytes`, `bsr_event`, `bsr_trigger`

These fields are not yet the final MCS/RB/modulation display. Treat them as the decoded transport-block layer that the next parser step will correlate with PHY scheduling logs.

### `lte_phy_pusch_tx_candidate`

Source: LTE `0xB139`, only when `--probe-phy` is enabled.

Use for parser/debug views, not final user KPI tiles yet:

- `tti`, `sfn_guess`, `subframe_guess`
- `grant`, confirmed to match LTE MAC UL `grant` on exact TTI in the T99W175 load capture
- `tx_power_raw`, `field_10_raw`, `field_34_raw`, `field_36_raw`, `field_40_raw`, `field_42_raw`, `field_46_raw`
- `record_hex`

Only `grant` and timing are mapped with confidence. Keep the other fields in a collapsible debug/details view until more captures prove the offsets.

### `measurement_database_update`

Source: NR `0xB97F`.

Use for NR signal cards:

- `layers[]`
- per layer: `nr_arfcn`, `cc_id`, `serving_pci`, `serving_ssb`
- per layer: `serving_rsrp_dbm[]`, `rx_beam[]`, `rfic_id`, `subarray[]`
- per cell/beam: `rsrp_dbm`, `rsrq_db`, filtered RSRP/RSRQ

## Missing Metric Placeholders

The GUI must render these as unavailable/pending, not zero:

- LTE: `dl_mcs`, `ul_mcs`
- LTE: `dl_modulation`, `ul_modulation`
- LTE: `dl_rb_alloc`, `ul_rb_alloc`
- LTE: `cqi`, `ri`, `pmi`
- LTE: `bler`, `tx_power_dbm`
- NR: `dl_mcs`, `ul_mcs`
- NR: `dl_modulation`, `ul_modulation`
- NR: `dl_rb_alloc`, `ul_rb_alloc`
- NR: `ss_rsrp_dbm`, `ss_rsrq_db`, `ss_sinr_db`
- NR: `per_rx_rsrp_dbm`, `per_rx_sinr_db`
- NR: `nr_mode`, `endc_anchor`, `rank_ri`, `cqi`, `tx_power_dbm`

Each placeholder has:

```json
{"value": null, "status": "not_decoded", "source_candidates": ["..."]}
```

The GUI should show `status` text or a muted placeholder. Do not coerce `null` to `0`.

## Polling Pitfalls

Production dashboard mode should consume `/tmp/qdiag-state.json`, not JSONL. `--raw` and `--debug` are useful while developing decoders, but they duplicate parsed events and can grow quickly.

The advanced signals page should poll the snapshot at most every 10 seconds and treat it as stale after 30 seconds. Avoid polling faster than 10 seconds unless the user explicitly opens a debug/profiling mode.

The DCI binary emits `qxdm_ts` inside nested records and `updated_at` at the snapshot level. Prefer top-level `updated_at` for GUI freshness and `qxdm_ts` for event ordering/details.

Combo payloads can be large. Snapshot mode saves them as files (`b0cd_qlte.hex`, `b826_qnr.hex`) and exposes only path, size, timestamp, and format. Do not copy full `payload_hex` into a frequently polled endpoint unless the page explicitly opens a debug/detail view.

For multi-cell displays, key LTE per-antenna entries by at least `earfcn + pci + cell_index`, not just event name. For NR, key layers/beams by `nr_arfcn + serving_pci + layer + ssb_index` when those fields are present.

## Known Live Validation

Validated on T99W175:

- `/dev/diag` + Qualcomm DCI works.
- `qdiagmon-dci --mac` emits signal and MAC JSON directly.
- Snapshot mode was live-tested with `--snapshot-file /tmp/qdiag-state.json --snapshot-interval-ms 10000 --no-raw-log --max-runtime-sec 25 --mac --combo-dir /tmp/qdiag-combos`: stdout/stderr stayed at 0 bytes, the snapshot grew from 381 bytes initial state to about 1.3 KB with LTE per-antenna and MAC data, no `.tmp` file was left behind, and the final snapshot had `runtime.running:false`.
- Forced Hetzner download through `enx00e04c6802a5` produced LTE MAC DL/UL events.
- QLTE and QNR combo payloads were captured as `b0cd_qlte.hex` and `b826_qnr.hex`.
- `--probe-scheduling` was tested; it produced measurement/search/RACH candidates, not confirmed MCS/RB/modulation.
- `--probe-phy` found active LTE PHY candidates. `0xB139` now has a partial decoder for PUSCH TX timing and UL grant.

Validated event examples are in `docs/gui-state.example.json`.
