from __future__ import annotations

import datetime as _dt
import struct
from dataclasses import dataclass
from typing import Iterable


DIAG_VERNO_F = 0x00
DIAG_LOG_F = 0x10
DIAG_EVENT_REPORT_F = 0x60
DIAG_LOG_CONFIG_F = 0x73
DIAG_EXT_BUILD_ID_F = 0x7C

LOG_CONFIG_DISABLE_OP = 0
LOG_CONFIG_RETRIEVE_ID_RANGES_OP = 1
LOG_CONFIG_SET_MASK_OP = 3

DIAG_SUBSYS_ID_LTE = 0x0B

CRC_TABLE = (
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
    0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
    0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
    0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
    0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
    0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
    0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
    0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
    0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
    0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
    0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
    0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
    0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
    0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
    0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
    0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
    0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
    0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
    0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
    0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
    0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
    0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
    0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
    0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
    0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
    0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
    0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
    0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
    0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
    0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78,
)


@dataclass(frozen=True)
class DiagLogPacket:
    log_id: int
    timestamp: _dt.datetime
    raw_timestamp: int
    body: bytes
    radio_id: int = 0


def dm_crc16(data: bytes) -> int:
    ret = 0xFFFF
    for byte in data:
        ret = (ret >> 8) ^ CRC_TABLE[(ret ^ byte) & 0xFF]
    return ret ^ 0xFFFF


def wrap(data: bytes) -> bytes:
    return data.replace(b"\x7d", b"\x7d\x5d").replace(b"\x7e", b"\x7d\x5e")


def unwrap(data: bytes) -> bytes:
    return data.replace(b"\x7d\x5e", b"\x7e").replace(b"\x7d\x5d", b"\x7d")


def generate_packet(payload: bytes) -> bytes:
    return wrap(payload + struct.pack("<H", dm_crc16(payload))) + b"\x7e"


def parse_qxdm_ts(ts: int) -> _dt.datetime:
    epoch = _dt.datetime(1980, 1, 6, tzinfo=_dt.timezone.utc)
    upper = ts >> 16
    lower = ts & 0xFFFF
    try:
        return epoch + _dt.timedelta(milliseconds=upper * 1.25 + lower / 40960)
    except OverflowError:
        return epoch


def iter_hdlc_frames(chunks: Iterable[bytes]) -> Iterable[bytes]:
    buf = b""
    for chunk in chunks:
        if not chunk:
            continue
        buf += chunk
        parts = buf.split(b"\x7e")
        buf = parts.pop()
        for part in parts:
            if part:
                yield part


def decode_frame(frame: bytes, check_crc: bool = True) -> bytes:
    pkt = unwrap(frame)
    if len(pkt) < 3:
        raise ValueError("short DIAG frame")
    payload, crc_bytes = pkt[:-2], pkt[-2:]
    got = struct.unpack("<H", crc_bytes)[0]
    want = dm_crc16(payload)
    if check_crc and got != want:
        raise ValueError(f"CRC mismatch: got 0x{got:04x}, expected 0x{want:04x}")
    return payload


def parse_log_packet(payload: bytes) -> DiagLogPacket | None:
    radio_id = 0
    if len(payload) >= 24 and payload[0] == 0x98 and payload[8] == DIAG_LOG_F:
        radio_id = payload[4]
        payload = payload[8:]
    if len(payload) < 16 or payload[0] != DIAG_LOG_F:
        return None
    cmd, _reserved, _length1, length2, log_id, ts = struct.unpack("<BBHHHQ", payload[:16])
    if cmd != DIAG_LOG_F:
        return None
    body_len = max(0, length2 - 12)
    return DiagLogPacket(log_id=log_id, timestamp=parse_qxdm_ts(ts), raw_timestamp=ts, body=payload[16:16 + body_len], radio_id=radio_id)


def parse_dci_log_record(payload: bytes, radio_id: int = 0) -> DiagLogPacket | None:
    if len(payload) < 12:
        return None
    length, log_id, ts_lo, ts_hi = struct.unpack("<HHLL", payload[:12])
    if length < 12:
        return None
    body = payload[12:min(length, len(payload))]
    return DiagLogPacket(log_id=log_id, timestamp=parse_qxdm_ts((ts_hi << 32) | ts_lo), raw_timestamp=(ts_hi << 32) | ts_lo, body=body, radio_id=radio_id)


def bytes_reqd_for_bit(bit: int) -> int:
    return bit // 8 + (1 if bit % 8 else 0)


def create_log_config_set_mask(equip_id: int, last_item: int, items: Iterable[int]) -> bytes:
    payload = bytearray(bytes_reqd_for_bit(last_item))
    for item in items:
        if item > last_item:
            continue
        payload[item // 8] |= 1 << (item % 8)
    return struct.pack("<LLLL", DIAG_LOG_CONFIG_F, LOG_CONFIG_SET_MASK_OP, equip_id, last_item) + bytes(payload)


def diag_init_packets(enable_mac: bool = False, probe_scheduling: bool = False, probe_phy: bool = False) -> list[bytes]:
    from .logcodes import metric_log_items

    return [
        generate_packet(struct.pack("<B", DIAG_VERNO_F)),
        generate_packet(struct.pack("<B", DIAG_EXT_BUILD_ID_F)),
        generate_packet(struct.pack("<BB", DIAG_EVENT_REPORT_F, 0)),
        generate_packet(struct.pack("<LL", DIAG_LOG_CONFIG_F, LOG_CONFIG_RETRIEVE_ID_RANGES_OP)),
        generate_packet(create_log_config_set_mask(DIAG_SUBSYS_ID_LTE, 0x09FF, metric_log_items(enable_mac=enable_mac, probe_scheduling=probe_scheduling, probe_phy=probe_phy))),
        generate_packet(struct.pack("<BB", DIAG_EVENT_REPORT_F, 1)),
    ]


def diag_stop_packets() -> list[bytes]:
    return [
        generate_packet(struct.pack("<BB", DIAG_EVENT_REPORT_F, 0)),
        generate_packet(struct.pack("<LL", DIAG_LOG_CONFIG_F, LOG_CONFIG_DISABLE_OP)),
    ]
