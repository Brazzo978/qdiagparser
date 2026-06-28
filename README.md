# qdiagparser

Qualcomm DIAG parser for Quectel/Foxconn-style modems, focused on radio metrics instead of GSMTAP output.

Current decoded logs:

- LTE `0xB063`/`0xB064`: MAC DL/UL transport blocks with TBS/grant/HARQ/CC/SFN/subframe activity fields.
- LTE `0xB17F`: serving EARFCN/PCI/RSRP/RSSI/RSRQ.
- LTE `0xB180`: neighbor cells with PCI/RSRP/RSSI/RSRQ.
- LTE `0xB193`: per-RX antenna RSRP/RSRQ/RSSI/SNR plus raw CINR fields. Versions 36/48/50 are decoded; version 59 is emitted as structured raw until its offsets are mapped.
- LTE `0xB197`, `0xB0C1`, `0xB0C2`: cell/MIB/serving info.
- NR `0xB97F`: layers, serving RSRP per RX, cells, beams, RSRP/RSRQ.
- NR `0xB822`, `0xB823`: MIB and serving cell info.
- LTE/NR `0xB0CD`/`0xB826`: raw combo payloads ready for HandyMenny `uecapabilityparser` (`QLTE`/`QNR`).

GUI handoff docs:

- `docs/GUI_HANDOFF.md`: state shape, event contract, placeholders, and runtime commands.
- `docs/SCHEDULING_PROBE.md`: current evidence for missing MCS/RB/modulation metrics and probe results.

## Run

From this checkout:

```bash
./qdiagparser --help
PYTHONPATH=src python3 -m qdiagparser --help
PYTHONPATH=src python3 -m qdiagparser mask
```

Parse a QMDL/HDLC dump:

```bash
PYTHONPATH=src python3 -m qdiagparser parse capture.qmdl > metrics.jsonl
```

Live USB DIAG on the T99W373 composition seen here (`05c6:90d5`, interface 0, OUT `0x01`, IN `0x81`):

```bash
PYTHONPATH=src python3 -m qdiagparser usb-capture \
  --pid 0x90d5 --interface 0 --ep-out 0x01 --ep-in 0x81 \
  --configure --stop --seconds 30 > metrics.jsonl
```

Serial DIAG, if exposed by another USB composition:

```bash
PYTHONPATH=src python3 -m qdiagparser capture -d /dev/ttyUSB2 --configure --stop --seconds 30
```

## Modem-Side Runtime

The modems do not ship `python3`, so the repo also builds native ARM hard-float binaries.

`qdiagmon-dci` is the preferred modem-side runtime when `/dev/diag` and `libdiag.so.1` are present, as on the T99W175. For the WebUI, run it in low-resource snapshot mode: it keeps the latest decoded state in memory and atomically overwrites one small JSON file.

```bash
make dci-docker MODEM_SYSROOT="/home/manu/Scrivania/Esim t99/analysis_work/extract/2024_system/rootfs"
adb push build/qdiagmon-dci-armhf /tmp/qdiagmon-dci
adb shell 'chmod +x /tmp/qdiagmon-dci'
adb shell '/tmp/qdiagmon-dci --snapshot-file /tmp/qdiag-state.json \
  --snapshot-interval-ms 10000 \
  --no-raw-log \
  --max-runtime-sec 600 \
  --mac \
  --combo-dir /tmp/qdiag-combos >/dev/null 2>/tmp/qdiag-state.err &'
```

Snapshot mode writes `/tmp/qdiag-state.json.tmp` and renames it over `/tmp/qdiag-state.json`, so the CGI can serve the state without parsing large logs:

```sh
#!/bin/sh
printf 'Content-Type: application/json\r\n\r\n'
cat /tmp/qdiag-state.json 2>/dev/null || \
  printf '{"updated_at":0,"stale_after_ms":30000,"lte":{"serving_cell":{},"per_antenna":[],"mac":{},"ca":{}},"nr":{"layers":[],"ca":{}},"runtime":{"running":false,"uptime_s":0,"events_seen":0,"last_error":"no snapshot yet"}}\n'
```

The GUI should poll every 10000 ms and mark data stale after 30000 ms. Do not delete the snapshot after each request; it is intentionally overwritten in place to avoid races and NAND churn.

For development captures, JSONL stream mode is still available:

```bash
adb shell '/tmp/qdiagmon-dci --seconds 30 --mac --combo-dir /tmp/qdiag-combos > /tmp/qdiag-live.jsonl'
adb pull /tmp/qdiag-live.jsonl ./qdiag-live.jsonl
adb pull /tmp/qdiag-combos ./qdiag-combos
```

The shell sampler remains only a fallback for old binaries that do not have `--snapshot-file`:

```bash
adb push scripts/qdiag-gui-sample.sh /tmp/qdiag-gui-sample.sh
adb shell 'chmod +x /tmp/qdiag-gui-sample.sh'
adb shell 'QDIAG_SECONDS=2 QDIAG_MAX_AGE=10 QDIAG_MAC=1 /tmp/qdiag-gui-sample.sh'
```

Experimental probe for missing scheduling metrics:

```bash
adb shell '/tmp/qdiagmon-dci --seconds 30 --mac --probe-scheduling > /tmp/qdiag-probe.jsonl'
```

Experimental PHY probe for missing MCS/RB/modulation/CQI/RI/PMI candidates:

```bash
adb shell '/tmp/qdiagmon-dci --seconds 60 --mac --probe-phy > /tmp/qdiag-phy.jsonl'
PYTHONPATH=src python3 -m qdiagparser mask --mac --probe-phy > /tmp/qdiag-mask-phy.hex
```

The static `qdiagmon-armhf` remains a raw-device fallback/debug tool:

```bash
make arm
adb push build/qdiagmon-armhf /tmp/qdiagmon
adb shell 'chmod +x /tmp/qdiagmon; /tmp/qdiagmon --help'
```

Run it on a modem DIAG character device:

```bash
adb shell '/tmp/qdiagmon -d /dev/smd11 --seconds 30 --stop --debug'
```

Write raw capability payloads for `uecapabilityparser`:

```bash
adb shell '/tmp/qdiagmon -d /dev/smd11 --wait-uecap both --combo-dir /tmp/uecap --stop'
adb pull /tmp/uecap ./uecapability
```

On the T99W373, `smd7` is AT via `port_bridge smd7 at_usb2 1`, `smd8` is the Compal AT CLI path, and DIAG is routed through `diag-router`/`/dev/ffs-diag`. On the T99W175, DIAG is the native kernel `/dev/diag` path and DCI works directly. Do not assume an SMD node is DIAG without probing; use the DCI/mdlog path first when available.

`diag_mdlog` is a useful portable fallback and produces QMDL2 (`0x98`) logs that this parser understands:

```bash
PYTHONPATH=src python3 - <<'PY'
from qdiagparser.diag import diag_init_packets
open('/tmp/qdiag-mask.cfg', 'wb').write(b''.join(diag_init_packets(enable_mac=True, probe_scheduling=True, probe_phy=True)))
PY
adb push /tmp/qdiag-mask.cfg /tmp/qdiag-mask.cfg
adb shell 'rm -rf /tmp/qdiagmdlog; mkdir -p /tmp/qdiagmdlog; diag_mdlog -f /tmp/qdiag-mask.cfg -o /tmp/qdiagmdlog -s 8 -c'
adb pull /tmp/qdiagmdlog ./qdiagmdlog
PYTHONPATH=src python3 -m qdiagparser parse ./qdiagmdlog/*.qmdl --probe-scheduling --probe-phy --combo-dir ./qdiag-combos > metrics.jsonl
```

## GUI Feed

For a quick GUI, use JSONL event mode:

```bash
./qdiagparser usb-capture --pid 0x90d5 --configure --debug --combo-dir ./uecap > metrics.jsonl
```

Or have the Python runtime maintain a latest-state file:

```bash
./qdiagparser usb-capture --pid 0x90d5 --configure \
  --state-file ./latest-signals.json --snapshot-interval 1
```

The state file groups latest values under `lte`, `nr`, `combos`, and `warnings`, which is intended for a WebUI polling endpoint.

## CA Combo Decoding

This project captures `0xB0CD` and `0xB826` as raw hex. For full supported-combo decoding, feed those payloads to HandyMenny `uecapabilityparser`:

```bash
uecapabilityparser cli -t QLTE -i b0cd.hex -j lte-combos.json
uecapabilityparser cli -t QNR -i b826.hex -j nr-combos.json
```

You can wait specifically for those records:

```bash
./qdiagparser wait-uecap --source usb --pid 0x90d5 --target both --out-dir ./uecapability --debug
./qdiagparser wait-uecap --source device -d /dev/smd11 --target both --out-dir /tmp/uecap --debug
```

`0xB0CD/0xB826` are supported capability lists, not necessarily the live PCC/SCC runtime state. Runtime serving/layer/beam values come from ML1/RRC logs such as `0xB17F`, `0xB193`, `0xB197`, `0xB823`, and `0xB97F`.

## Live Probe Status

T99W373 validation: ADB reported Linux `sdxlemur` and USB composition `DIAG_ADB_MBIM_GNSS_DUN`; AT reports `T99W373` / `FDE.F0.3.0.1.1.VF.001`. `smd7` is AT bridged, `smd8` answers AT, and DIAG is behind `diag-router`/FFS. QMDL2 parsing works and sees `0xB193` v59 as structured raw.

T99W175 validation: ADB reported Linux `sdxprairie`, distro `mdm 202402210955`; AT reports `T99W175.F0.6.0.0.6.VF.009`. `/dev/diag` exists, `diag_mdlog` captures QMDL2 logs, and `qdiagmon-dci` emits live JSON. A 12 second DCI run produced 291 JSON events and wrote `b0cd_qlte.hex` (8407 bytes). Observed serving LTE examples include EARFCN 525/PCI 174 and EARFCN 1850/PCI 300 with per-RX RSRP/RSRQ/SNR.

## Tests

```bash
PYTHONPATH=src python3 -m unittest discover -s tests
```
