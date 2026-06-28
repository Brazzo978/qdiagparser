from __future__ import annotations

import argparse
import pathlib
import json
import sys
import time

from .diag import DIAG_VERNO_F, decode_frame, diag_init_packets, diag_stop_packets, generate_packet, iter_hdlc_frames, parse_log_packet
from .io import FileDiagSource, UsbDiagSource, default_device_candidates, open_device_source
from .logcodes import PROBE_PHY_LOG_IDS, PROBE_SCHEDULING_LOG_IDS
from .metrics import MetricsParser
from .state import MetricsState


def emit(events: list[dict], pretty: bool = False) -> None:
    try:
        for event in events:
            if pretty:
                print(json.dumps(event, indent=2, sort_keys=True))
            else:
                print(json.dumps(event, separators=(",", ":"), sort_keys=True))
            sys.stdout.flush()
    except BrokenPipeError:
        raise SystemExit(0)


def emit_one(event: dict, pretty: bool = False) -> None:
    emit([event], pretty)


def write_combo_file(combo_dir: str, event: dict) -> str | None:
    if event.get("event") != "supported_ca_combos_raw":
        return None
    fmt = event.get("format")
    if fmt not in ("QLTE", "QNR"):
        return None
    target = pathlib.Path(combo_dir)
    target.mkdir(parents=True, exist_ok=True)
    name = "b0cd_qlte.hex" if fmt == "QLTE" else "b826_qnr.hex"
    path = target / name
    path.write_text(event["payload_hex"] + "\n")
    return str(path)


def parse_frames(
    chunks,
    check_crc: bool,
    pretty: bool,
    debug: bool = False,
    state_file: str | None = None,
    snapshot_interval: float = 1.0,
    combo_dir: str | None = None,
    wait_uecap: set[str] | None = None,
    probe_scheduling: bool = False,
    probe_phy: bool = False,
) -> int:
    parser = MetricsParser()
    state = MetricsState()
    next_snapshot = time.monotonic() + snapshot_interval
    seen_uecap: set[str] = set()
    count = 0
    for frame in iter_hdlc_frames(chunks):
        try:
            payload = decode_frame(frame, check_crc=check_crc)
        except ValueError as exc:
            emit([{"event": "frame_error", "reason": str(exc)}], pretty)
            continue
        pkt = parse_log_packet(payload)
        if pkt is None:
            if debug:
                emit_one({"event": "diag_control", "cmd": f"0x{payload[0]:02X}", "payload_hex": payload.hex()}, pretty)
            continue
        events = parser.parse(pkt)
        if (
            debug
            or (probe_scheduling and pkt.log_id in PROBE_SCHEDULING_LOG_IDS)
            or (probe_phy and pkt.log_id in PROBE_PHY_LOG_IDS)
        ) and not events:
            events = [{
                "time": pkt.timestamp.isoformat(),
                "event": "diag_log_raw",
                "log_id": f"0x{pkt.log_id:04X}",
                "body_len": len(pkt.body),
                "body_hex": pkt.body.hex(),
            }]
        if events:
            for event in events:
                state.update(event)
                if combo_dir:
                    combo_path = write_combo_file(combo_dir, event)
                    if combo_path and debug:
                        emit_one({"event": "combo_file_written", "path": combo_path, "format": event.get("format")}, pretty)
                if event.get("event") == "supported_ca_combos_raw":
                    if event.get("format") == "QLTE":
                        seen_uecap.add("lte")
                    elif event.get("format") == "QNR":
                        seen_uecap.add("nr")
            emit(events, pretty)
            count += len(events)
        if state_file and time.monotonic() >= next_snapshot:
            state.write(state_file)
            next_snapshot = time.monotonic() + snapshot_interval
        if wait_uecap and wait_uecap.issubset(seen_uecap):
            break
    if state_file:
        state.write(state_file)
    return count


def cmd_capture(args: argparse.Namespace) -> int:
    src = open_device_source(args.device, raw=args.raw, baudrate=args.baudrate, timeout=args.timeout)
    try:
        if args.configure:
            for pkt in diag_init_packets(enable_mac=args.mac, probe_scheduling=args.probe_scheduling, probe_phy=args.probe_phy):
                src.write(pkt)
        return 0 if parse_frames(
            src.chunks(duration=args.seconds),
            check_crc=not args.no_crc,
            pretty=args.pretty,
            debug=args.debug,
            state_file=args.state_file,
            snapshot_interval=args.snapshot_interval,
            combo_dir=args.combo_dir,
            probe_scheduling=args.probe_scheduling,
            probe_phy=args.probe_phy,
        ) >= 0 else 1
    finally:
        if args.stop:
            for pkt in diag_stop_packets():
                src.write(pkt)
        src.close()


def cmd_usb_capture(args: argparse.Namespace) -> int:
    src = UsbDiagSource(
        vid=int(args.vid, 0),
        pid=int(args.pid, 0) if args.pid else None,
        interface=args.interface,
        ep_out=int(args.ep_out, 0),
        ep_in=int(args.ep_in, 0),
        timeout_ms=args.timeout_ms,
    )
    try:
        if args.configure:
            for pkt in diag_init_packets(enable_mac=args.mac, probe_scheduling=args.probe_scheduling, probe_phy=args.probe_phy):
                src.write(pkt)
        return 0 if parse_frames(
            src.chunks(duration=args.seconds),
            check_crc=not args.no_crc,
            pretty=args.pretty,
            debug=args.debug,
            state_file=args.state_file,
            snapshot_interval=args.snapshot_interval,
            combo_dir=args.combo_dir,
            probe_scheduling=args.probe_scheduling,
            probe_phy=args.probe_phy,
        ) >= 0 else 1
    finally:
        if args.stop:
            for pkt in diag_stop_packets():
                src.write(pkt)
        src.close()


def cmd_parse(args: argparse.Namespace) -> int:
    src = FileDiagSource(args.files)
    parse_frames(
        src.chunks(),
        check_crc=not args.no_crc,
        pretty=args.pretty,
        debug=args.debug,
        state_file=args.state_file,
        combo_dir=args.combo_dir,
        probe_scheduling=args.probe_scheduling,
        probe_phy=args.probe_phy,
    )
    return 0


def cmd_mask(args: argparse.Namespace) -> int:
    for pkt in diag_init_packets(enable_mac=args.mac, probe_scheduling=args.probe_scheduling, probe_phy=args.probe_phy):
        print(pkt.hex())
    return 0


def probe_source(path: str, raw: bool, baudrate: int, timeout: float, seconds: float) -> dict:
    result = {"path": path, "ok": False, "frames": 0, "diag_payloads": [], "error": None}
    try:
        src = open_device_source(path, raw=raw, baudrate=baudrate, timeout=timeout)
    except Exception as exc:
        result["error"] = str(exc)
        return result
    try:
        src.write(generate_packet(bytes([DIAG_VERNO_F])))
        for frame in iter_hdlc_frames(src.chunks(duration=seconds)):
            result["frames"] += 1
            try:
                payload = decode_frame(frame, check_crc=True)
            except ValueError as exc:
                result.setdefault("crc_errors", []).append(str(exc))
                continue
            result["diag_payloads"].append(payload[:32].hex())
            if payload and payload[0] in (DIAG_VERNO_F, 0x13):
                result["ok"] = True
                break
    except Exception as exc:
        result["error"] = str(exc)
    finally:
        src.close()
    return result


def cmd_probe(args: argparse.Namespace) -> int:
    candidates = args.candidate or default_device_candidates()
    for path in candidates:
        result = probe_source(path, raw=args.raw, baudrate=args.baudrate, timeout=args.timeout, seconds=args.seconds)
        emit_one(result, pretty=args.pretty)
        if args.first and result["ok"]:
            break
    return 0


def auto_device(args: argparse.Namespace) -> str:
    for path in args.candidate or default_device_candidates():
        result = probe_source(path, raw=args.raw, baudrate=args.baudrate, timeout=args.timeout, seconds=args.probe_seconds)
        if result["ok"]:
            return path
    raise RuntimeError("no DIAG device found; run probe --pretty for details")


def cmd_modem(args: argparse.Namespace) -> int:
    device = auto_device(args) if args.device == "auto" else args.device
    if args.debug:
        emit_one({"event": "selected_device", "path": device}, args.pretty)
    args.device = device
    return cmd_capture(args)


def cmd_wait_uecap(args: argparse.Namespace) -> int:
    targets = {"lte", "nr"} if args.target == "both" else {args.target}
    if args.source == "usb":
        src = UsbDiagSource(
            vid=int(args.vid, 0),
            pid=int(args.pid, 0) if args.pid else None,
            interface=args.interface,
            ep_out=int(args.ep_out, 0),
            ep_in=int(args.ep_in, 0),
            timeout_ms=args.timeout_ms,
        )
    else:
        device = auto_device(args) if args.device == "auto" else args.device
        src = open_device_source(device, raw=args.raw, baudrate=args.baudrate, timeout=args.timeout)
        if args.debug:
            emit_one({"event": "selected_device", "path": device}, args.pretty)
    try:
        for pkt in diag_init_packets(enable_mac=False):
            src.write(pkt)
        parse_frames(
            src.chunks(duration=args.seconds),
            check_crc=not args.no_crc,
            pretty=args.pretty,
            debug=args.debug,
            combo_dir=args.out_dir,
            wait_uecap=targets,
        )
        return 0
    finally:
        if args.stop:
            for pkt in diag_stop_packets():
                src.write(pkt)
        src.close()


def add_common_capture_options(ap: argparse.ArgumentParser, device_required: bool = True) -> None:
    ap.add_argument("-d", "--device", required=device_required, default=None if device_required else "auto", help="DIAG device, or auto")
    ap.add_argument("--raw", action="store_true", help="use raw os.read/os.write instead of pyserial")
    ap.add_argument("-b", "--baudrate", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=0.2)
    ap.add_argument("-s", "--seconds", type=float, default=None, help="capture duration; default runs until interrupted")
    ap.add_argument("--configure", action=argparse.BooleanOptionalAction, default=False, help="send metric log masks before reading")
    ap.add_argument("--stop", action="store_true", help="disable DIAG logs on exit")
    ap.add_argument("--mac", action="store_true", help="include LTE MAC DL/UL transport block log mask")
    ap.add_argument("--probe-scheduling", action="store_true", help="include experimental scheduling candidate logs and emit them raw")
    ap.add_argument("--probe-phy", action="store_true", help="include experimental LTE PHY candidate logs and emit them raw")
    ap.add_argument("--no-crc", action="store_true", help="do not validate QCDM CRC")
    ap.add_argument("--debug", action="store_true", help="emit control frames, unknown logs, and helper events as JSON")
    ap.add_argument("--state-file", help="write latest-state JSON for a GUI")
    ap.add_argument("--snapshot-interval", type=float, default=1.0)
    ap.add_argument("--combo-dir", help="write B0CD/B826 raw hex files when seen")
    ap.add_argument("--pretty", action="store_true")


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Qualcomm DIAG LTE/NR metric parser")
    sub = p.add_subparsers(dest="cmd", required=True)

    cap = sub.add_parser("capture", help="read live DIAG from a serial/USB DIAG port")
    add_common_capture_options(cap)
    cap.set_defaults(func=cmd_capture)

    usb = sub.add_parser("usb-capture", help="read live DIAG from a vendor USB DIAG interface")
    usb.add_argument("--vid", default="0x05c6")
    usb.add_argument("--pid", default=None, help="optional product id, for example 0x90d5")
    usb.add_argument("--interface", type=int, default=0)
    usb.add_argument("--ep-out", default="0x01")
    usb.add_argument("--ep-in", default="0x81")
    usb.add_argument("--timeout-ms", type=int, default=200)
    usb.add_argument("-s", "--seconds", type=float, default=None, help="capture duration; default runs until interrupted")
    usb.add_argument("--configure", action="store_true", help="send metric log masks before reading")
    usb.add_argument("--stop", action="store_true", help="disable DIAG logs on exit")
    usb.add_argument("--mac", action="store_true", help="include LTE MAC DL/UL transport block log mask")
    usb.add_argument("--probe-scheduling", action="store_true", help="include experimental scheduling candidate logs and emit them raw")
    usb.add_argument("--probe-phy", action="store_true", help="include experimental LTE PHY candidate logs and emit them raw")
    usb.add_argument("--no-crc", action="store_true", help="do not validate QCDM CRC")
    usb.add_argument("--debug", action="store_true", help="emit control frames, unknown logs, and helper events as JSON")
    usb.add_argument("--state-file", help="write latest-state JSON for a GUI")
    usb.add_argument("--snapshot-interval", type=float, default=1.0)
    usb.add_argument("--combo-dir", help="write B0CD/B826 raw hex files when seen")
    usb.add_argument("--pretty", action="store_true")
    usb.set_defaults(func=cmd_usb_capture)

    parse = sub.add_parser("parse", help="parse QMDL/HDLC DIAG dump files")
    parse.add_argument("files", nargs="+")
    parse.add_argument("--no-crc", action="store_true")
    parse.add_argument("--debug", action="store_true")
    parse.add_argument("--probe-scheduling", action="store_true", help="emit raw scheduling candidate logs")
    parse.add_argument("--probe-phy", action="store_true", help="emit raw LTE PHY candidate logs")
    parse.add_argument("--state-file")
    parse.add_argument("--combo-dir")
    parse.add_argument("--pretty", action="store_true")
    parse.set_defaults(func=cmd_parse)

    mask = sub.add_parser("mask", help="print init/config packets as hex")
    mask.add_argument("--mac", action="store_true")
    mask.add_argument("--probe-scheduling", action="store_true")
    mask.add_argument("--probe-phy", action="store_true")
    mask.set_defaults(func=cmd_mask)

    probe = sub.add_parser("probe", help="probe modem-side candidate devices for DIAG")
    probe.add_argument("-c", "--candidate", action="append", help="candidate path; can be repeated")
    probe.add_argument("--raw", action="store_true", help="force raw os.read/os.write for all candidates")
    probe.add_argument("-b", "--baudrate", type=int, default=115200)
    probe.add_argument("--timeout", type=float, default=0.2)
    probe.add_argument("-s", "--seconds", type=float, default=1.0)
    probe.add_argument("--first", action="store_true", help="stop after first working DIAG device")
    probe.add_argument("--pretty", action="store_true")
    probe.set_defaults(func=cmd_probe)

    modem = sub.add_parser("modem", help="modem-side capture with auto DIAG device selection")
    add_common_capture_options(modem, device_required=False)
    modem.add_argument("-c", "--candidate", action="append", help="candidate path; can be repeated")
    modem.add_argument("--probe-seconds", type=float, default=1.0)
    modem.set_defaults(func=cmd_modem, configure=True)

    wait = sub.add_parser("wait-uecap", help="wait for B0CD/B826 and write uecapabilityparser hex inputs")
    wait.add_argument("--source", choices=("device", "usb"), default="device")
    wait.add_argument("--target", choices=("lte", "nr", "both"), default="both")
    wait.add_argument("--out-dir", default="uecapability")
    wait.add_argument("-d", "--device", default="auto")
    wait.add_argument("-c", "--candidate", action="append")
    wait.add_argument("--raw", action="store_true")
    wait.add_argument("-b", "--baudrate", type=int, default=115200)
    wait.add_argument("--timeout", type=float, default=0.2)
    wait.add_argument("--probe-seconds", type=float, default=1.0)
    wait.add_argument("--vid", default="0x05c6")
    wait.add_argument("--pid", default=None)
    wait.add_argument("--interface", type=int, default=0)
    wait.add_argument("--ep-out", default="0x01")
    wait.add_argument("--ep-in", default="0x81")
    wait.add_argument("--timeout-ms", type=int, default=200)
    wait.add_argument("-s", "--seconds", type=float, default=None)
    wait.add_argument("--stop", action="store_true")
    wait.add_argument("--no-crc", action="store_true")
    wait.add_argument("--debug", action="store_true")
    wait.add_argument("--pretty", action="store_true")
    wait.set_defaults(func=cmd_wait_uecap)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    return args.func(args)
