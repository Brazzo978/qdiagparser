#!/bin/sh
set -eu

# Lightweight sampler for a modem WebUI.
# Do not run qdiagmon-dci as a permanent daemon from the GUI: DIAG streaming,
# especially probe modes, can burn CPU on the module.

BIN="${QDIAG_BIN:-/tmp/qdiagmon-dci}"
OUT_DIR="${QDIAG_OUT_DIR:-/tmp/qdiag-gui}"
MAX_AGE="${QDIAG_MAX_AGE:-10}"
SECONDS="${QDIAG_SECONDS:-2}"
WITH_MAC="${QDIAG_MAC:-1}"
WITH_PROBE_PHY="${QDIAG_PROBE_PHY:-0}"

LATEST="$OUT_DIR/latest.jsonl"
STAMP="$OUT_DIR/latest.ts"
TMP="$OUT_DIR/latest.jsonl.tmp"
LOCK="$OUT_DIR/lock"

mkdir -p "$OUT_DIR"
now="$(date +%s)"

if [ -s "$LATEST" ] && [ -s "$STAMP" ]; then
    last="$(cat "$STAMP" 2>/dev/null || echo 0)"
    age=$((now - last))
    if [ "$age" -lt "$MAX_AGE" ]; then
        cat "$LATEST"
        exit 0
    fi
fi

if ! mkdir "$LOCK" 2>/dev/null; then
    if [ -s "$LATEST" ]; then
        cat "$LATEST"
        exit 0
    fi
    echo '{"event":"qdiag_sampler_busy","reason":"capture already running"}'
    exit 0
fi
trap 'rmdir "$LOCK" 2>/dev/null || true' EXIT INT TERM

args="--seconds $SECONDS"
if [ "$WITH_MAC" = "1" ]; then
    args="$args --mac"
fi
if [ "$WITH_PROBE_PHY" = "1" ]; then
    args="$args --probe-phy"
fi

"$BIN" $args > "$TMP" 2>"$OUT_DIR/latest.err" || true
mv "$TMP" "$LATEST"
date +%s > "$STAMP"
cat "$LATEST"
