# GUI Handoff

This file is the contract for a modem-side advanced signal GUI fed by `qdiagmon-dci`.

## Runtime

Preferred runtime on T99W175-style firmware:

```sh
/tmp/qdiagmon-dci --mac --combo-dir /tmp/qdiag-combos > /tmp/qdiag-live.jsonl 2>/tmp/qdiag-live.err
```

Do not use that continuous command directly from a production GUI page. It is for development captures. On the module, DIAG streaming can be CPU-heavy if it is left running.

Preferred GUI sampling policy:

```sh
QDIAG_SECONDS=2 QDIAG_MAX_AGE=10 QDIAG_MAC=1 QDIAG_PROBE_PHY=0 \
  /tmp/qdiag-gui-sample.sh > /tmp/qdiag-gui-response.jsonl
```

This runs a short one-shot capture only when the cache is older than 10 seconds. If multiple page requests arrive together, the sampler returns the cached JSONL instead of starting a second DIAG reader.

For a quick finite capture:

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

On a Python-capable host, use the state writer:

```sh
PYTHONPATH=src python3 -m qdiagparser parse capture.qmdl \
  --state-file latest-signals.json \
  --combo-dir ./qdiag-combos
```

The modem-side C runtime writes JSONL events only. A GUI can either consume the stream directly or run a tiny collector that keeps the latest event per metric and writes the same state shape described below.

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
- `combos`: latest raw `QLTE`/`QNR` capability payloads.
- `missing_metrics`: stable placeholders for fields not decoded yet.
- `warnings`: last parser warnings.
- `meta`: collector timestamps and event count. A modem-side collector should also add `source`, `parser_version`, and `stale_after_ms` when available.

Important nested keys:

- `lte.per_antenna["0"]`: latest LTE per-RX metrics for cell index `0`.
- `lte.mac.dl`: latest decoded LTE DL transport block.
- `lte.mac.ul`: latest decoded LTE UL transport block.
- `nr.ml1_latest`: latest NR ML1 measurement database update.

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

Production dashboard mode should consume normal JSONL, not debug/raw JSONL. `--raw` and `--debug` are useful while developing decoders, but they duplicate parsed events and can grow quickly.

The advanced signals page should not keep `qdiagmon-dci` running permanently. Recommended defaults are a 2 second capture every 10 seconds, with cached results served between captures. Avoid polling faster than 10 seconds unless the user explicitly opens a debug/profiling mode.

The DCI binary emits `qxdm_ts`; the Python parser emits ISO `time`. A GUI or collector should accept either. Prefer `qxdm_ts` for event ordering when present, and mark tiles stale when no fresh event arrives for roughly `2-3` seconds.

Combo payloads can be large. Save them as files (`b0cd_qlte.hex`, `b826_qnr.hex`) and expose path, size, timestamp, and format in the UI. Do not copy the full `payload_hex` into a frequently polled state endpoint unless the page explicitly opens a debug/detail view.

For multi-cell displays, key LTE per-antenna entries by at least `earfcn + pci + cell_index`, not just event name. For NR, key layers/beams by `nr_arfcn + serving_pci + layer + ssb_index` when those fields are present.

## Known Live Validation

Validated on T99W175:

- `/dev/diag` + Qualcomm DCI works.
- `qdiagmon-dci --mac` emits signal and MAC JSON directly.
- Forced Hetzner download through `enx00e04c6802a5` produced LTE MAC DL/UL events.
- QLTE and QNR combo payloads were captured as `b0cd_qlte.hex` and `b826_qnr.hex`.
- `--probe-scheduling` was tested; it produced measurement/search/RACH candidates, not confirmed MCS/RB/modulation.
- `--probe-phy` found active LTE PHY candidates. `0xB139` now has a partial decoder for PUSCH TX timing and UL grant.

Validated event examples are in `docs/gui-state.example.json`.
