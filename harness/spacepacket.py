"""Python version of the packet format, written from docs/packet_format.md
rather than the C++ so the two can catch each other's mistakes."""

from __future__ import annotations

import struct
from dataclasses import dataclass

SYNC_MARKER = bytes.fromhex("1ACFFC1D")
HOUSEKEEPING_APID = 0x101
EVENT_APID = 0x102
MODES = {"off": 0, "boot": 1, "ready": 2, "safe": 3}


def crc16_ccitt(data: bytes, crc: int = 0xFFFF) -> int:
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def encode(apid: int, seq: int, seconds: float, user_data: bytes) -> bytes:
    coarse = int(seconds)
    fine = round((seconds - coarse) * 65536)
    if fine == 65536:
        coarse, fine = coarse + 1, 0
    data_field_len = 6 + len(user_data) + 2
    header = struct.pack(">HHH", (1 << 11) | apid, (0b11 << 14) | seq, data_field_len - 1)
    body = header + struct.pack(">IH", coarse, fine) + user_data
    return body + struct.pack(">H", crc16_ccitt(body))


@dataclass
class Housekeeping:
    mode: str = "ready"
    detector_temp_c: float = -40.0
    bus_voltage_v: float = 28.0
    current_a: float = 3.4
    azimuth_deg: float = 0.0
    elevation_deg: float = 0.0
    dead_pixels: int = 0
    cooler_ready: bool = True
    gimbal_moving: bool = False
    bit_fault: bool = False

    def pack(self) -> bytes:
        flags = (self.cooler_ready << 0) | (self.gimbal_moving << 1) | (self.bit_fault << 2)
        return struct.pack(
            ">HhHHiiHBB", MODES[self.mode], round(self.detector_temp_c * 100), round(self.bus_voltage_v * 1000),
            round(self.current_a * 1000), round(self.azimuth_deg * 1000), round(self.elevation_deg * 1000),
            self.dead_pixels, flags, 0)


def event(severity: int, text: str) -> bytes:
    raw = text.encode("ascii")
    return struct.pack(">BB", severity, len(raw)) + raw
