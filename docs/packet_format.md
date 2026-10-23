# Telemetry packet format

Both the C++ library (`include/tlm/`) and the Python test encoder (`harness/spacepacket.py`) are written from this.

## Space Packet (CCSDS 133.0-B-2)
All fields are big-endian.

| Bits | Field | Value |
|---|---|---|
| 3 | Version | `000` |
| 1 | Type | `0` = telemetry |
| 1 | Secondary header flag | `1` (required: carries the timestamp) |
| 11 | APID (application process ID) | `0x000`-`0x7FF` |
| 2 | Sequence flags | `11` = unsegmented |
| 14 | Sequence count | per APID, wraps 16383 to 0 |
| 16 | Packet data length | (data field bytes) - 1 |

Data field:

| Bytes | Field |
|---|---|
| 4 | Time, coarse: seconds since mission epoch (CUC) |
| 2 | Time, fine: 1/65536 s |
| 0-65520 | User data (by APID) |
| 2 | Packet error control: CRC-16/CCITT-FALSE over all preceding bytes |

CRC-16/CCITT-FALSE uses polynomial 0x1021, initial value 0xFFFF, no reflection and no final XOR. Its check value is `"123456789"` = `0x29B1`.

## APID 0x101: Housekeeping (20 bytes)
| Offset | Type | Field | Scale |
|---|---|---|---|
| 0 | u16 | mode (0 off, 1 boot, 2 ready, 3 safe) | |
| 2 | i16 | detector temperature | 0.01 °C |
| 4 | u16 | bus voltage | 1 mV |
| 6 | u16 | current | 1 mA |
| 8 | i32 | azimuth | 0.001° |
| 12 | i32 | elevation | 0.001° |
| 16 | u16 | dead pixel count | |
| 18 | u8 | flags: bit0 cooler ready, bit1 gimbal moving, bit2 BIT fault | |
| 19 | u8 | spare (0) | |

Unknown modes, undefined flag bits or a non-zero spare byte are rejected as `bad_payload_field`.

## APID 0x102: Event (2-257 bytes)
| Offset | Type | Field |
|---|---|---|
| 0 | u8 | severity (0 info, 1 warning, 2 error, 3 critical) |
| 1 | u8 | text length N (0-255) |
| 2 | N bytes | printable ASCII text |

## Transport
- UDP: one packet per datagram, no framing.
- Streams (serial, TCP, recordings): every packet is preceded by the CCSDS Attached Sync Marker `1A CF FC 1D`. The depacketizer re-synchronizes on the next marker after garbage or a corrupt packet.

## Decode order
Length, then CRC, then the header fields. That way a flipped version or type bit shows up as `crc_mismatch` instead of `bad_version`, and the header checks only catch packets the sender actually built wrong.

## Known limitations
- A corrupted length field in a stream can make the depacketizer wait for up to 64 KiB before the CRC proves the packet bad. Following packets are then recovered, but delayed. A transfer-frame layer with fixed-size frames (CCSDS TM frames) removes this; it is out of scope here.
- Tail loss (the last packets of a session) cannot be detected from sequence counts alone.
