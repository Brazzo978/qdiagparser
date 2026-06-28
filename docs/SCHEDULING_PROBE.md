# Scheduling Probe Notes

Goal: find the missing real-time fields for an NSG-like GUI: MCS, RB allocation, modulation, CQI/RI/PMI, BLER, and TX power.

## What Is Already Functional

The current minimum functional GUI can display:

- LTE serving signal from `0xB17F`.
- LTE per-RX antenna signal from `0xB193`.
- LTE serving/band/bandwidth from `0xB0C2` and related cell info logs.
- NR layer/cell/beam measurements from `0xB97F` when emitted.
- NR serving/band/bandwidth from `0xB823` when emitted.
- LTE MAC traffic activity from `0xB063`/`0xB064`:
  - DL `tbs_bytes`, `padding_bytes`, `cc_id`, `harq_id`, `sfn`, `subframe`.
  - UL `grant`, `rlc_pdus`, `padding_bytes`, `harq_id`, `sfn`, `subframe`.
- LTE/NR supported combo payloads from `0xB0CD`/`0xB826`.

`0xB063`/`0xB064` are useful for traffic activity and throughput estimation, but they do not directly expose final MCS/RB/modulation in the Qualcomm parser structures we have from SCAT.

## Probe Mode

Both runtimes now support an experimental scheduling probe.

Modem-side DCI:

```sh
/tmp/qdiagmon-dci --seconds 30 --mac --probe-scheduling > /tmp/qdiag-probe.jsonl
```

Python/diag_mdlog mask:

```sh
PYTHONPATH=src python3 -m qdiagparser mask --mac --probe-scheduling > /tmp/qdiag-mask-probe.hex
```

Python parser raw candidate output:

```sh
PYTHONPATH=src python3 -m qdiagparser parse capture.qmdl --probe-scheduling > probe.jsonl
```

PHY candidate probe:

```sh
/tmp/qdiagmon-dci --seconds 60 --mac --probe-phy > /tmp/qdiag-phy.jsonl
PYTHONPATH=src python3 -m qdiagparser mask --mac --probe-phy > /tmp/qdiag-mask-phy.hex
PYTHONPATH=src python3 -m qdiagparser parse capture.qmdl --probe-phy > phy.jsonl
```

## Candidate Logs Enabled

- LTE `0xB167`: ML1 MAC RAR Msg1 report.
- LTE `0xB168`: ML1 MAC RAR Msg2 report.
- LTE `0xB169`: ML1 MAC UE identification Msg3 report.
- LTE `0xB16A`: ML1 MAC contention resolution Msg4 report.
- LTE `0xB179`: ML1 connected-mode intra-frequency measurements.
- LTE `0xB192`: ML1 neighbor cell measurement request/response.
- LTE `0xB194`: ML1 search request/response.
- LTE `0xB195`: ML1 connected neighbor measurement request/response.
- NR `0xB88A`: NR MAC RACH attempt.

These names come from SCAT/Qualcomm log code mappings. They are probe candidates, not confirmed MCS/RB logs.

## PHY Candidate Logs Enabled

These log IDs are present in the local Qualcomm/QXDM golden logmask and are more likely to contain scheduling or link-adaptation fields than the first measurement/search probe. Names are working labels for investigation until offsets are proven.

- LTE `0xB126`: PDSCH demapper/config candidate.
- LTE `0xB130`: PDCCH decoding result candidate.
- LTE `0xB132`: PDCCH decoding result candidate.
- LTE `0xB139`: PUSCH TX report candidate.
- LTE `0xB13C`: PUCCH TX report candidate.
- LTE `0xB140`: PUSCH CSF report candidate.
- LTE `0xB144`: PDSCH stat indication candidate.
- LTE `0xB16B`: PDCCH/PHICH indication candidate.
- LTE `0xB16D`: GM TX report candidate.
- LTE `0xB173`: PDSCH stat indication candidate.
- LTE `0xB174`: PUSCH stat indication candidate.
- LTE `0xB175`: PUCCH CSF report candidate.
- LTE `0xB176`: CQI report candidate.
- LTE `0xB177`: RI report candidate.
- LTE `0xB178`: PMI report candidate.

## Live PHY Probe On T99W175

Probe command:

```sh
/tmp/qdiagmon-dci --seconds 75 --mac --probe-phy > /tmp/qdiag-phy/load.jsonl
curl -4 --interface enx00e04c6802a5 --resolve fsn1-speed.hetzner.com:443:78.46.170.2 \
  -L --connect-timeout 15 --max-time 60 -o /dev/null https://fsn1-speed.hetzner.com/1GB.bin
```

Download validation:

- `local_ip=192.168.225.54`
- `remote_ip=78.46.170.2`
- `http_code=200`
- `size_download=810909438`
- `speed_download=13515337`

Idle capture:

- Duration: 20 seconds.
- Events: 1155.
- Error file: empty.

Load capture:

- Duration: 75 seconds.
- Events: 3312.
- Error file: empty.

High-signal raw log growth, idle to load:

- `0xB130`: 238 to 518.
- `0xB132`: 36 to 122.
- `0xB139`: 76 to 257.
- `0xB13C`: 120 to 558.
- `0xB16B`: 22 to 65.
- `0xB16D`: 26 to 93.
- `0xB173`: 33 to 125.

Low-yield in this run:

- `0xB144`: 2 idle, 2 load.
- `0xB177`: 2 idle, 2 load.
- `0xB140`, `0xB174`, `0xB175`, `0xB176`, `0xB178`: no events seen in this T99W175 run.

Confirmed partial mapping:

- `0xB139` is structured as an 8-byte header plus 100-byte records.
- In each `0xB139` record, offset `+0` is a TTI counter compatible with `SFN * 10 + subframe`.
- In each `0xB139` record, offset `+8` matches the decoded LTE MAC UL `grant` field. In the load capture it matched 153 out of 165 exact TTI-correlated records.
- `0xB139` is now emitted as `lte_phy_pusch_tx_candidate` with `tti`, `sfn_guess`, `subframe_guess`, `grant`, and raw offset fields.

Structured decoder smoke:

- Command: `/tmp/qdiagmon-dci --seconds 35 --mac --probe-phy`
- Download: Hetzner via `enx00e04c6802a5`, `local_ip=192.168.225.54`, `size_download=382254846`, `speed_download=15289823`.
- Output: 2158 JSONL events, stderr empty.
- `lte_phy_pusch_tx_candidate`: 272 records, 160 with non-zero `grant`.

Promising but not confirmed:

- `0xB130` is structured as a 4-byte header plus 32-byte records and looks like a PDCCH/DCI family, but it does not correlate one-to-one with decoded DL MAC transport blocks.
- `0xB173` is structured as a 4-byte header plus 40-byte records. Record offset `+16` often has LTE-MCS-like values such as `7`, `15`, `18`, and `26`, but exact DL MAC correlation is not proven yet.
- `0xB132` carries similar candidate values and scales under load; it needs a separate controlled downlink capture before exposing an MCS field.

## Live Probe On T99W175

Idle capture:

- Duration: 20 seconds.
- Output: 506 JSONL events.
- Error file: empty.

Idle log counts:

- `0xB193`: 209
- `0xB064`: 94
- `0xB194`: 74
- `0xB195`: 47
- `0xB179`: 47
- `0xB063`: 17
- `0xB826`: 14
- `0xB167`: 1
- `0xB168`: 1
- `0xB169`: 1
- `0xB88A`: 1

Load capture:

- Download: Hetzner forced through `enx00e04c6802a5`.
- Curl confirmed `local_ip=192.168.225.54`.
- Downloaded 802176766 bytes in 60 seconds before expected timeout.
- Average curl speed: 13369795 bytes/s.
- DIAG output: 1873 JSONL events.
- Error file: empty.

Load log counts:

- `0xB193`: 1192
- `0xB064`: 215
- `0xB194`: 160
- `0xB195`: 100
- `0xB179`: 100
- `0xB063`: 51
- `0xB0CD`: 28
- `0xB826`: 14
- `0xB17F`: 2
- `0xB192`: 2
- `0xB167`: 2
- `0xB168`: 2
- `0xB169`: 2
- `0xB0C2`: 1
- `0xB16A`: 1
- `0xB88A`: 1

## Interpretation

The candidate probe proves extra ML1/search/RACH logs are available on the T99W175, and some increase under traffic. However:

- `0xB179`, `0xB194`, `0xB195`, and `0xB192` look like measurement/search request-response families.
- `0xB167`..`0xB16A` and `0xB88A` are RACH/access related.
- None is confirmed as the real-time PHY scheduling log that directly carries MCS/RB/modulation.

Therefore the honest minimum functional GUI should show:

- Full signal metrics already decoded.
- LTE MAC traffic activity with TBS/grant/HARQ/CC.
- MCS/RB/modulation/CQI/RI/PMI as explicit `not_decoded` placeholders.

## Next Search Direction

The missing fields likely require a different Qualcomm log family not present in SCAT's decoded Qualcomm LTE/NR parser:

- LTE/NR PDSCH decode or scheduling report.
- LTE/NR PUSCH Tx report.
- DCI decode/grant report.
- CQI/PMI/RI report.
- BLER/HARQ statistics.

Use `--probe-scheduling` as the safe baseline, then expand with any known QXDM log IDs for PDSCH/PUSCH/DCI/CQI if found. Keep dashboard mode separate from probe mode to avoid large JSONL output.
