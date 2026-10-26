"""Python sends packets over UDP with drops/dups/reorders/corruption and we
check tlm_decode reports exactly that.

    TLM_DECODE=build/gcc/tlm_decode python3 -m pytest harness
"""

from __future__ import annotations

import json
import os
import random
import socket
import subprocess
import time
from pathlib import Path

import pytest

from spacepacket import EVENT_APID, HOUSEKEEPING_APID, SYNC_MARKER, Housekeeping, encode, event

# has to be absolute or Windows won't launch it (relative path + forward slashes)
DECODER = str(Path(os.environ.get("TLM_DECODE", "build/gcc/tlm_decode")).resolve())


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def run_udp(datagrams: list[bytes], rate_hz: float | None = 5000) -> list[dict]:
    """Start the decoder, send datagrams at a telemetry-like rate (None = one burst), return its JSON lines."""
    if not Path(DECODER).exists():
        pytest.skip(f"decoder not built: {DECODER}")
    port = free_port()
    proc = subprocess.Popen([DECODER, "--udp", str(port), "--idle-timeout-ms", "400"],
                            stdout=subprocess.PIPE, text=True)
    ready = json.loads(proc.stdout.readline())
    assert ready["type"] == "ready" and ready["port"] == port and ready["rcvbuf"] > 0
    interval = 1 / rate_hz if rate_hz else 0
    next_send = time.perf_counter()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for datagram in datagrams:
            if interval:
                while time.perf_counter() < next_send:
                    pass
                next_send += interval
            sock.sendto(datagram, ("127.0.0.1", port))
    out, _ = proc.communicate(timeout=20)
    assert proc.returncode == 0
    return [json.loads(line) for line in out.splitlines()]


def by_type(lines: list[dict], kind: str) -> list[dict]:
    return [line for line in lines if line["type"] == kind]


def hk_packets(seqs, start_time: float = 1000.0, rng: random.Random | None = None):
    rng = rng or random.Random(1)
    sent = {}
    for seq in seqs:
        hk = Housekeeping(
            mode=rng.choice(["boot", "ready", "safe"]),
            detector_temp_c=round(rng.uniform(-45, 30), 2),
            bus_voltage_v=round(rng.uniform(26, 30), 3),
            current_a=round(rng.uniform(0.5, 4), 3),
            azimuth_deg=round(rng.uniform(-170, 170), 3),
            elevation_deg=round(rng.uniform(-30, 90), 3),
            dead_pixels=rng.randrange(0, 200),
            cooler_ready=rng.random() < 0.5,
            gimbal_moving=rng.random() < 0.5,
            bit_fault=rng.random() < 0.1,
        )
        sent[seq] = (hk, encode(HOUSEKEEPING_APID, seq % 16384, start_time + seq * 0.5, hk.pack()))
    return sent


def test_values_survive_the_round_trip():
    sent = hk_packets(range(300))
    lines = run_udp([packet for _, packet in sent.values()])
    packets = by_type(lines, "packet")
    assert len(packets) == 300 and not by_type(lines, "error") and not by_type(lines, "sequence")
    for p in packets:
        hk = sent[p["seq"]][0]
        d = p["data"]
        assert d["mode"] == hk.mode
        assert d["detector_temp_c"] == pytest.approx(hk.detector_temp_c, abs=0.005)
        assert d["bus_voltage_v"] == pytest.approx(hk.bus_voltage_v, abs=0.0005)
        assert d["azimuth_deg"] == pytest.approx(hk.azimuth_deg, abs=0.0005)
        assert d["dead_pixels"] == hk.dead_pixels
        assert (d["cooler_ready"], d["gimbal_moving"], d["bit_fault"]) == (hk.cooler_ready, hk.gimbal_moving, hk.bit_fault)
        assert p["time"] == pytest.approx(1000.0 + p["seq"] * 0.5)


def test_lost_packets_reported_with_counts():
    sent = hk_packets(range(100))
    dropped = {10, 50, 51, 52}
    lines = run_udp([pkt for seq, (_, pkt) in sent.items() if seq not in dropped])
    gaps = [(s["expected"], s["lost"]) for s in by_type(lines, "sequence")]
    assert gaps == [(10, 1), (50, 3)]
    assert by_type(lines, "summary")[0]["apids"][str(HOUSEKEEPING_APID)]["lost"] == 4


def test_duplicates_and_reordering():
    sent = hk_packets(range(10))
    order = [0, 1, 2, 2, 4, 3, 5]  # 2 duplicated; 3 arrives after 4
    lines = run_udp([sent[s][1] for s in order])
    events = [(s["status"], s["received"]) for s in by_type(lines, "sequence")]
    assert events == [("duplicate", 2), ("gap", 4), ("out_of_order", 3)]
    stats = by_type(lines, "summary")[0]["apids"][str(HOUSEKEEPING_APID)]
    assert (stats["duplicates"], stats["out_of_order"], stats["lost"]) == (1, 1, 1)


def test_corrupted_packet_rejected_and_counted_as_lost():
    sent = hk_packets(range(20))
    datagrams = [pkt for _, pkt in sent.values()]
    bad = bytearray(datagrams[7])
    bad[20] ^= 0x10  # one bit flip in the payload
    datagrams[7] = bytes(bad)
    lines = run_udp(datagrams)
    assert [e["error"] for e in by_type(lines, "error")] == ["crc_mismatch"]
    assert [(s["status"], s["lost"]) for s in by_type(lines, "sequence")] == [("gap", 1)]
    assert 7 not in {p["seq"] for p in by_type(lines, "packet")}


def test_sequence_counter_wraparound_is_not_a_gap():
    sent = hk_packets(range(16380, 16390))  # crosses 16383 -> 0
    lines = run_udp([pkt for _, pkt in sent.values()])
    assert len(by_type(lines, "packet")) == 10 and not by_type(lines, "sequence")


def test_variable_length_event_packets():
    texts = ["Cooler READY", "Gimbal limit reached on axis AZ", "x" * 255, ""]
    datagrams = [encode(EVENT_APID, i, 50.0, event(i % 4, t)) for i, t in enumerate(texts)]
    events = [p["data"] for p in by_type(run_udp(datagrams), "packet")]
    assert [e["text"] for e in events] == texts
    assert [e["severity"] for e in events] == [0, 1, 2, 3]


def test_unknown_apid_passed_through_as_raw():
    lines = run_udp([encode(0x3FF, 0, 1.0, bytes(range(12)))])
    assert by_type(lines, "packet")[0] == {"type": "packet", "apid": 0x3FF, "seq": 0, "time": 1.0,
                                           "kind": "raw", "bytes": 12}


def test_stream_mode_resyncs_through_garbage(tmp_path):
    if not Path(DECODER).exists():
        pytest.skip(f"decoder not built: {DECODER}")
    sent = hk_packets(range(50))
    stream = bytearray()
    rng = random.Random(9)
    garbage = 0
    for seq, (_, pkt) in sent.items():
        if seq % 10 == 5:
            noise = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 30)))
            noise = noise.replace(SYNC_MARKER[:1], b"\x00")  # never start a fake marker
            stream += noise
            garbage += len(noise)
        stream += SYNC_MARKER + pkt
    recording = tmp_path / "pass.bin"
    recording.write_bytes(bytes(stream))
    out = subprocess.run([DECODER, "--stream", str(recording)], capture_output=True, text=True, check=True).stdout
    lines = [json.loads(line) for line in out.splitlines()]
    assert [p["seq"] for p in by_type(lines, "packet")] == list(range(50))
    assert by_type(lines, "summary")[0]["skipped_bytes"] == garbage


def test_overload_losses_are_all_reported():
    """Burst far faster than the receiver can drain. UDP will drop datagrams;
    the decoder must account for every one it saw a gap for."""
    sent = hk_packets(range(3000))
    lines = run_udp([pkt for _, pkt in sent.values()], rate_hz=None)
    seqs = [p["seq"] for p in by_type(lines, "packet")]
    stats = by_type(lines, "summary")[0]["apids"][str(HOUSEKEEPING_APID)]
    assert seqs == sorted(seqs)  # loopback may drop but does not reorder
    # every packet between the first and last received is either received or reported lost
    assert stats["received"] + stats["lost"] == seqs[-1] - seqs[0] + 1
    print(f"burst: {len(seqs)} of 3000 received, {stats['lost']} reported lost")
