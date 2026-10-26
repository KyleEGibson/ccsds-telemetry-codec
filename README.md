# ccsds-telemetry-codec

[![CI](https://github.com/KyleEGibson/ccsds-telemetry-codec/actions/workflows/ci.yml/badge.svg)](https://github.com/KyleEGibson/ccsds-telemetry-codec/actions/workflows/ci.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-lightgrey)

C++17 library and ground-side decoder for CCSDS Space Packets, the telemetry format used on spacecraft and a lot of aerospace/defense systems. It encodes and decodes packets with a CUC time header and CRC-16, pulls packets out of a noisy byte stream, and flags lost, duplicated and out-of-order packets.

Testing is layered: unit tests against the standard's check values, a separate Python encoder that drives the C++ decoder over a deliberately bad UDP link, sanitizers, and libFuzzer.

## What's in here
| | |
|---|---|
| `include/tlm/space_packet.hpp` | encode/decode one packet. Errors are returned as values, not thrown, since corrupt input is normal on a link. |
| `include/tlm/crc16.hpp` | CRC-16/CCITT-FALSE with a compile-time lookup table |
| `include/tlm/payloads.hpp` | housekeeping (scaled integers) and event (variable length) payloads |
| `include/tlm/sequence.hpp` | per-APID gap/duplicate/out-of-order detection, handles the 14-bit wrap |
| `include/tlm/stream.hpp` | sync-marker depacketizer that reassembles split packets and resyncs after garbage |
| `tools/tlm_decode.cpp` | UDP or stream receiver that prints JSON lines (POSIX and Winsock) |
| `harness/` | Python encoder + pytest scenarios over UDP |
| `fuzz/decode_fuzzer.cpp` | libFuzzer target with a round-trip check |

Packet layout and field encodings are in [`docs/packet_format.md`](docs/packet_format.md).

## Testing
| Layer | Checks | Result |
|---|---|---|
| GoogleTest (40 tests) | CRC check value `0x29B1`, exact header bit layout, every single-bit flip in a packet rejected, wraparound, split/garbage/corrupt streams | pass on GCC and Clang |
| ASan + UBSan | whole suite, no out-of-bounds or UB | clean |
| UDP harness (9 scenarios) | Python and C++ agree on 300 random packets; drops, dups, reorders and corruption each reported exactly; stream resync counts the garbage bytes | pass on Linux and native Windows |
| libFuzzer (60-120 s) | ~475k inputs/min, no crashes, anything decode accepts re-encodes to the same bytes | clean |
| `-Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Werror`, MSVC `/W4 /WX` | no warnings | enforced |

### Things testing turned up
- UDP was dropping packets silently under a burst: 79 of 300 lost with the default socket buffer. The decoder now asks for 8 MB and prints what the OS actually gave it. After that a 3,000-packet burst came through complete, and there's an overload test that checks every packet is either received or reported lost.
- Decode order matters. Length and CRC are checked before the header fields, so a flipped version bit is reported as `crc_mismatch` instead of a misleading `bad_version`.
- The harness wouldn't launch the decoder on Windows until I resolved its path to absolute. Windows can't start a relative path written with forward slashes.

## Build and test
Everything runs in a container so you don't need a local compiler:
```bash
docker build -t tlm-dev -f docker/Dockerfile .
docker run --rm -v "$PWD:/src" -w /src tlm-dev ./scripts/ci.sh          # everything + 30 s of fuzzing
docker run --rm -v "$PWD:/src" -w /src tlm-dev ./scripts/ci.sh quick    # no fuzzing
```
Native build (GCC, Clang or MSVC, CMake 3.20+):
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
TLM_DECODE=build/tlm_decode python -m pytest harness
```
Decoding live telemetry:
```bash
build/tlm_decode --udp 5555
{"type":"ready","port":5555,"rcvbuf":8388608}
{"type":"packet","apid":257,"seq":12,"time":1006,"kind":"housekeeping","data":{"mode":"ready","detector_temp_c":-40.12,...}}
{"type":"sequence","apid":257,"status":"gap","expected":13,"received":15,"lost":2}
{"type":"error","error":"crc_mismatch","bytes":34}
```

## Skills it covers
- C++17: RAII, `std::variant` results, `constexpr` tables, cross-platform sockets, strict warnings
- integration testing across two languages with two separate implementations of one spec
- sanitizers, fuzzing with a round-trip check, impaired-link testing
- pinned toolchain container, GoogleTest pinned by version and SHA-256
