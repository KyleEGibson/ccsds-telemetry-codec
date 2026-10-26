// tlm_decode: receive telemetry and print one JSON object per line.
//
//   tlm_decode --udp 5555 [--idle-timeout-ms 1000] [--max-packets N] [--rcvbuf-kb 8192]
//       one Space Packet per UDP datagram (no sync marker)
//   tlm_decode --stream recording.bin     (or "-" for stdin)
//       sync-marker framed byte stream
//
// Output lines:
//   {"type":"ready","port":5555,"rcvbuf":212992}
//   {"type":"packet","apid":257,"seq":12,"time":1700.5,"kind":"housekeeping","data":{...}}
//   {"type":"sequence","apid":257,"status":"gap","expected":13,"received":15,"lost":2}
//   {"type":"error","error":"crc_mismatch","bytes":34}
//   {"type":"summary",...}   always last

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "tlm/payloads.hpp"
#include "tlm/sequence.hpp"
#include "tlm/space_packet.hpp"
#include "tlm/stream.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;  // socklen_t comes from ws2tcpip.h
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
#endif

namespace {

std::string json_escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

const char* mode_name(tlm::Mode mode) {
    switch (mode) {
        case tlm::Mode::Off: return "off";
        case tlm::Mode::Boot: return "boot";
        case tlm::Mode::Ready: return "ready";
        case tlm::Mode::Safe: return "safe";
    }
    return "unknown";
}

class Reporter {
public:
    void packet(const tlm::SpacePacket& packet) {
        const auto& h = packet.header;
        const auto seq = monitor_.observe(h.apid, h.sequence_count);
        if (seq.status != tlm::SequenceStatus::First && seq.status != tlm::SequenceStatus::InOrder) {
            std::cout << "{\"type\":\"sequence\",\"apid\":" << h.apid << ",\"status\":\"" << to_string(seq.status)
                      << "\",\"expected\":" << seq.expected << ",\"received\":" << seq.received
                      << ",\"lost\":" << seq.lost << "}\n";
        }
        std::ostringstream line;
        line.precision(10);
        line << "{\"type\":\"packet\",\"apid\":" << h.apid << ",\"seq\":" << h.sequence_count
             << ",\"time\":" << packet.time.seconds();
        if (h.apid == tlm::kHousekeepingApid) {
            const auto hk = tlm::unpack_housekeeping(packet.user_data);
            if (!hk.ok()) return error(hk.error(), packet.user_data.size());
            const auto& v = hk.value();
            line << ",\"kind\":\"housekeeping\",\"data\":{\"mode\":\"" << mode_name(v.mode)
                 << "\",\"detector_temp_c\":" << v.detector_temp_c << ",\"bus_voltage_v\":" << v.bus_voltage_v
                 << ",\"current_a\":" << v.current_a << ",\"azimuth_deg\":" << v.azimuth_deg
                 << ",\"elevation_deg\":" << v.elevation_deg << ",\"dead_pixels\":" << v.dead_pixels
                 << ",\"cooler_ready\":" << (v.cooler_ready ? "true" : "false")
                 << ",\"gimbal_moving\":" << (v.gimbal_moving ? "true" : "false")
                 << ",\"bit_fault\":" << (v.bit_fault ? "true" : "false") << "}";
        } else if (h.apid == tlm::kEventApid) {
            const auto ev = tlm::unpack_event(packet.user_data);
            if (!ev.ok()) return error(ev.error(), packet.user_data.size());
            line << ",\"kind\":\"event\",\"data\":{\"severity\":" << static_cast<int>(ev.value().severity)
                 << ",\"text\":\"" << json_escape(ev.value().text) << "\"}";
        } else {
            line << ",\"kind\":\"raw\",\"bytes\":" << packet.user_data.size();
        }
        line << "}";
        std::cout << line.str() << "\n";
        ++packets_;
    }

    void error(tlm::DecodeError err, std::size_t bytes) {
        std::cout << "{\"type\":\"error\",\"error\":\"" << to_string(err) << "\",\"bytes\":" << bytes << "}\n";
        ++errors_[to_string(err)];
    }

    void summary(std::size_t skipped_bytes) {
        std::cout << "{\"type\":\"summary\",\"packets\":" << packets_ << ",\"skipped_bytes\":" << skipped_bytes
                  << ",\"errors\":{";
        bool first = true;
        for (const auto& [name, count] : errors_) {
            std::cout << (first ? "" : ",") << "\"" << name << "\":" << count;
            first = false;
        }
        std::cout << "},\"apids\":{";
        first = true;
        for (const auto& [apid, s] : monitor_.stats()) {
            std::cout << (first ? "" : ",") << "\"" << apid << "\":{\"received\":" << s.received
                      << ",\"lost\":" << s.lost << ",\"duplicates\":" << s.duplicates
                      << ",\"out_of_order\":" << s.out_of_order << "}";
            first = false;
        }
        std::cout << "}}" << std::endl;
    }

private:
    tlm::SequenceMonitor monitor_;
    std::uint64_t packets_ = 0;
    std::map<std::string, std::uint64_t> errors_;
};

int run_stream(const std::string& path) {
    std::ifstream file;
    std::istream* in = &std::cin;
    if (path != "-") {
        file.open(path, std::ios::binary);
        if (!file) {
            std::cerr << "cannot open " << path << "\n";
            return 2;
        }
        in = &file;
    }
    Reporter reporter;
    tlm::Depacketizer depacketizer;
    std::vector<char> chunk(4096);
    while (*in) {
        in->read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = static_cast<std::size_t>(in->gcount());
        for (auto& item : depacketizer.push(reinterpret_cast<const std::uint8_t*>(chunk.data()), got)) {
            if (item.packet.ok()) reporter.packet(item.packet.value());
            else reporter.error(item.packet.error(), 0);
        }
    }
    reporter.summary(depacketizer.total_skipped() + depacketizer.buffered());
    return 0;
}

int run_udp(int port, int idle_timeout_ms, long max_packets, int rcvbuf_bytes) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 2;
#endif
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), static_cast<socklen_t>(sizeof addr)) != 0) {
        std::cerr << "cannot bind UDP port " << port << "\n";
        return 2;
    }
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(idle_timeout_ms);
#else
    timeval timeout{idle_timeout_ms / 1000, (idle_timeout_ms % 1000) * 1000};
#endif
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), static_cast<socklen_t>(sizeof timeout));

    // UDP drops silently when the receive buffer fills up (lost 79 of 300 in a
    // burst with the default). Ask for a big one and print what we got; Linux
    // caps it at net.core.rmem_max.
    int requested = rcvbuf_bytes, granted = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&requested), static_cast<socklen_t>(sizeof requested));
    auto granted_len = static_cast<socklen_t>(sizeof granted);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&granted), &granted_len);

    std::cout << "{\"type\":\"ready\",\"port\":" << port << ",\"rcvbuf\":" << granted << "}" << std::endl;
    Reporter reporter;
    std::vector<std::uint8_t> buffer(tlm::kMaxPacketSize);
    long received = 0;
    bool started = false;
    const auto start = std::chrono::steady_clock::now();
    while (max_packets <= 0 || received < max_packets) {
#ifdef _WIN32
        const auto n = recv(sock, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
#else
        const auto n = recv(sock, buffer.data(), buffer.size(), 0);
#endif
        if (n < 0) {
            // timeout: stop once traffic has started and gone quiet (or after 30 s of nothing)
            const auto waited = std::chrono::steady_clock::now() - start;
            if (started || waited > std::chrono::seconds(30)) break;
            continue;
        }
        started = true;
        ++received;
        const auto result = tlm::decode(buffer.data(), static_cast<std::size_t>(n));
        if (result.ok()) reporter.packet(result.value());
        else reporter.error(result.error(), static_cast<std::size_t>(n));
    }
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    reporter.summary(0);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    int port = 0, idle_timeout_ms = 1000, rcvbuf_kb = 8192;
    long max_packets = 0;
    std::string stream_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--udp" && has_value) port = std::atoi(argv[++i]);
        else if (arg == "--idle-timeout-ms" && has_value) idle_timeout_ms = std::atoi(argv[++i]);
        else if (arg == "--max-packets" && has_value) max_packets = std::atol(argv[++i]);
        else if (arg == "--stream" && has_value) stream_path = argv[++i];
        else if (arg == "--rcvbuf-kb" && has_value) rcvbuf_kb = std::atoi(argv[++i]);
        else {
            std::cerr << "usage: tlm_decode --udp PORT [--idle-timeout-ms MS] [--max-packets N]\n"
                         "       tlm_decode --stream FILE|-\n";
            return 2;
        }
    }
    if (!stream_path.empty()) return run_stream(stream_path);
    if (port > 0) return run_udp(port, idle_timeout_ms, max_packets, rcvbuf_kb * 1024);
    std::cerr << "choose --udp PORT or --stream FILE\n";
    return 2;
}
