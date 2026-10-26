// Application payloads carried in the user data field, selected by APID.
// Field encodings are listed in docs/packet_format.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tlm/space_packet.hpp"

namespace tlm {

constexpr std::uint16_t kHousekeepingApid = 0x101;
constexpr std::uint16_t kEventApid = 0x102;
constexpr std::size_t kHousekeepingSize = 20;
constexpr std::size_t kMaxEventText = 255;

enum class Mode : std::uint16_t { Off = 0, Boot = 1, Ready = 2, Safe = 3 };

// Engineering values. On the wire they are scaled integers, so values are
// quantized: 0.01 C, 1 mV, 1 mA, 0.001 deg.
struct Housekeeping {
    Mode mode = Mode::Off;
    double detector_temp_c = 0.0;
    double bus_voltage_v = 0.0;
    double current_a = 0.0;
    double azimuth_deg = 0.0;
    double elevation_deg = 0.0;
    std::uint16_t dead_pixels = 0;
    bool cooler_ready = false;
    bool gimbal_moving = false;
    bool bit_fault = false;
};

struct Event {
    std::uint8_t severity = 0;  // 0 info, 1 warning, 2 error, 3 critical
    std::string text;
};

// Pack throws std::out_of_range when a value cannot be represented.
std::vector<std::uint8_t> pack(const Housekeeping& hk);
std::vector<std::uint8_t> pack(const Event& event);

Result<Housekeeping> unpack_housekeeping(const std::vector<std::uint8_t>& user_data);
Result<Event> unpack_event(const std::vector<std::uint8_t>& user_data);

}  // namespace tlm
