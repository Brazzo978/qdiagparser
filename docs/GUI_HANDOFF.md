# GUI Handoff

This file is the contract for a modem-side advanced signal GUI fed by `qdiagmon-dci`.

## Runtime

Preferred runtime on T99W175-style firmware is on-demand snapshot refresh. The GUI/CGI should return the existing snapshot while it is fresh; when it is stale, launch one bounded collection and then return the new snapshot:

```sh
/tmp/qdiagmon-dci \
  --snapshot-file /tmp/qdiag-state.json \
  --snapshot-interval-ms 10000 \
  --stale-after-ms 30000 \
  --oneshot \
  --require signal \
  --sample-min-ms 500 \
  --max-runtime-sec 10 \
  --mac \
  --gui-lite \
  --no-raw-log \
  >/dev/null 2>/tmp/qdiag-state.err
cat /tmp/qdiag-state.json
```

This is the production WebUI path for very small modem CPUs. The binary collects DIAG only until the requested fresh packet arrives, updates an in-memory aggregate state, atomically writes a small snapshot, disables DIAG, and exits. If the required packet does not arrive within `--max-runtime-sec`, it writes `runtime.last_error` and exits non-zero.

Recommended request modes:

- `--require signal`: best default for the signals page. It exits as soon as a fresh signal packet arrives.
- `--require signal-mac`: use only when the page needs fresh MAC activity too; it can wait longer if traffic is idle.
- `--require mac`: debug/traffic views only.
- `--require any`: fastest smoke check, but weaker for GUI correctness.

For a resident sampler instead of on-demand CGI refresh, omit `--oneshot`. Snapshot mode defaults to a light duty cycle: `--gui-lite`, `--sample-window-ms 2000`, and `--nice 5`. It disables the DIAG log mask after the requested fresh data arrives, then sleeps until the next 10 second cycle.

Snapshot writing remains atomic:

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
    "phy": {
      "pusch_tx_candidate": {},
      "pdsch_stat_candidate": {}
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
  "missing_metrics": {
    "lte": {
      "dl_mcs": {"value": null, "status": "not_decoded", "source_candidates": ["LTE 0xB130/0xB132/0xB144/0xB173"]}
    },
    "nr": {
      "dl_mcs": {"value": null, "status": "not_decoded", "source_candidates": ["NR MAC/PHY scheduling logs"]}
    }
  },
  "runtime": {
    "running": true,
    "uptime_s": 120,
    "events_seen": 4520,
    "snapshot_interval_ms": 10000,
    "sample_window_ms": 10000,
    "sample_min_ms": 500,
    "oneshot": true,
    "require": "signal",
    "sample_events_seen": 19,
    "sample_signal_seen": true,
    "sample_mac_seen": true,
    "diag_stream_active": false,
    "gui_lite": true,
    "nice_increment": 5,
    "last_error": "",
    "log_counts": []
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

Keep CA combo capture out of the 10 second signal refresh path too. Use `--wait-uecap`/`--combo-dir` as a separate user-triggered action, not in the normal GUI polling loop.

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
- LTE PHY candidate/debug: `lte_phy_pdsch_stat_candidate` only in probe/debug views
- LTE serving info: `serving_cell_info`
- NR ML1 measurements: `measurement_database_update`
- NR serving info: `serving_cell_info` where `rat == "NR"`
- CA capability payloads: `supported_ca_combos_raw`
- Warnings/debug: `parse_warning`, `diag_log_raw` only in debug mode

## State Shape

Top-level keys:

- `lte`: latest LTE metrics and helper groups.
- `nr`: latest NR metrics and helper groups.
- `missing_metrics`: GUI-ready placeholders for values not decoded yet.
- `runtime`: process status, uptime, event count, and last error.
- `updated_at` and `stale_after_ms`: top-level freshness contract for the GUI.

The modem snapshot repeats `missing_metrics` intentionally. It is still small enough for a 10 second poll, and it lets the GUI render missing values consistently without hardcoding the current research status.

Important nested keys:

- `lte.per_antenna[]`: latest LTE per-RX metrics keyed internally by `earfcn + pci + cell_index`.
- `lte.mac.dl_by_cc[]`: latest decoded LTE DL transport block per component carrier.
- `lte.mac.ul_by_cc[]`: latest decoded LTE UL transport block per component carrier.
- `lte.phy.pusch_tx_candidate`: latest `0xB139` PUSCH candidate, with timing/grant confidence only.
- `lte.phy.pdsch_stat_candidate`: latest `0xB173` PDSCH-stat candidate, raw/unconfirmed.
- `lte.ca.observed_cc_ids[]`: component carriers observed from MAC activity, already normalized for 2CA/3CA/4CA views.
- `nr.serving_cell`: latest NR serving cell info when emitted by the modem.
- `nr.layers[]`: reserved for NR layer metrics; currently empty in the C snapshot until the NR ML1 decoder is promoted into the runtime.
- `runtime.log_counts[]`: per-log counters for the current run, useful in probe/debug views to see which candidate logs are alive.

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

### `lte_phy_pdsch_stat_candidate`

Source: LTE `0xB173`, only when `--probe-phy` is enabled.

Use for parser/debug views only:

- `mcs_candidate_raw`, from record offset `+16`
- `tti_guess`, `field_0_raw`, `field_2_raw`, `field_4_raw`, `field_8_raw`, `field_12_raw`, `field_16_raw`, `field_18_raw`, `field_20_raw`, `field_24_raw`, `field_28_raw`
- `confidence: "candidate_unconfirmed"`

Do not display this as real MCS yet. Live probes showed the log is active, but current `mcs_candidate_raw` values can exceed the LTE MCS range, so the offset or record interpretation is still unproven.

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
- Snapshot mode with `missing_metrics`, `runtime.log_counts`, and `--probe-phy` was live-tested for 20 seconds: stdout/stderr stayed at 0 bytes, snapshot size was about 6.9 KB, and active logs included `0xB130`, `0xB139`, `0xB13C`, `0xB16B`, `0xB16D`, and `0xB173`.
- A 22 second light-traffic run through `enx00e04c6802a5` produced a 7.6 KB snapshot and 5404 DIAG events. Top probe counts were `0xB130:2703`, `0xB139:477`, `0xB13C:415`, `0xB16B:398`, `0xB16D:167`, and `0xB173:53`.
- On-demand GUI mode was live-tested with `--oneshot --require signal --sample-min-ms 500 --max-runtime-sec 10 --mac --gui-lite`: it exited successfully in about 1 second, saw 19 DIAG events, wrote a 4.5 KB snapshot, and left no running process.
- Final on-demand build was re-tested with the same profile: it exited in about 1 second, saw 9 DIAG events, wrote a 4.2 KB snapshot, had `runtime.diag_stream_active:false`, and left no running process.
- Resident duty-cycle mode was live-tested with `--sample-window-ms 2000 --sample-min-ms 500 --max-runtime-sec 12 --mac --gui-lite`: it saw 14 DIAG events total, wrote a 4.4 KB snapshot, and ended with `runtime.diag_stream_active:false`.
- Failure behavior was live-tested with `--oneshot --require mac --max-runtime-sec 3 --gui-lite` without `--mac`: it exited with code `2`, wrote `runtime.last_error:"sample timeout: require=mac not seen in 3000 ms"`, and left no running process.
- Forced Hetzner download through `enx00e04c6802a5` produced LTE MAC DL/UL events.
- QLTE and QNR combo payloads were captured as `b0cd_qlte.hex` and `b826_qnr.hex`.
- `--probe-scheduling` was tested; it produced measurement/search/RACH candidates, not confirmed MCS/RB/modulation.
- `--probe-phy` found active LTE PHY candidates. `0xB139` now has a partial decoder for PUSCH TX timing and UL grant.

Validated event examples are in `docs/gui-state.example.json`.
