#include "tlm/payloads.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "tlm/byteio.hpp"

namespace tlm {
namespace {

template <class Int>
Int scaled(double value, double scale, const char* name) {
    const double raw = std::round(value * scale);
    if (!std::isfinite(raw) || raw < static_cast<double>(std::numeric_limits<Int>::min()) ||
        raw > static_cast<double>(std::numeric_limits<Int>::max())) {
        throw std::out_of_range(std::string(name) + " cannot be represented");
    }
    return static_cast<Int>(raw);
}

constexpr std::uint8_t kCoolerReady = 0x01;
constexpr std::uint8_t kGimbalMoving = 0x02;
constexpr std::uint8_t kBitFault = 0x04;
constexpr std::uint8_t kKnownFlags = kCoolerReady | kGimbalMoving | kBitFault;

}  // namespace

std::vector<std::uint8_t> pack(const Housekeeping& hk) {
    std::vector<std::uint8_t> out;
    out.reserve(kHousekeepingSize);
    ByteWriter w(out);
    w.u16(static_cast<std::uint16_t>(hk.mode));
    w.i16(scaled<std::int16_t>(hk.detector_temp_c, 100.0, "detector_temp_c"));
    w.u16(scaled<std::uint16_t>(hk.bus_voltage_v, 1000.0, "bus_voltage_v"));
    w.u16(scaled<std::uint16_t>(hk.current_a, 1000.0, "current_a"));
    w.i32(scaled<std::int32_t>(hk.azimuth_deg, 1000.0, "azimuth_deg"));
    w.i32(scaled<std::int32_t>(hk.elevation_deg, 1000.0, "elevation_deg"));
    w.u16(hk.dead_pixels);
    w.u8(static_cast<std::uint8_t>((hk.cooler_ready ? kCoolerReady : 0) | (hk.gimbal_moving ? kGimbalMoving : 0) |
                                   (hk.bit_fault ? kBitFault : 0)));
    w.u8(0);  // spare, keeps the payload an even length
    return out;
}

Result<Housekeeping> unpack_housekeeping(const std::vector<std::uint8_t>& user_data) {
    if (user_data.size() != kHousekeepingSize) return DecodeError::BadPayloadSize;
    ByteReader r(user_data.data(), user_data.size());
    std::uint16_t mode = 0, voltage = 0, current = 0;
    std::int16_t temp = 0;
    std::int32_t az = 0, el = 0;
    std::uint8_t flags = 0, spare = 0;
    Housekeeping hk;
    r.u16(mode);
    r.i16(temp);
    r.u16(voltage);
    r.u16(current);
    r.i32(az);
    r.i32(el);
    r.u16(hk.dead_pixels);
    r.u8(flags);
    r.u8(spare);
    if (mode > static_cast<std::uint16_t>(Mode::Safe) || (flags & ~kKnownFlags) != 0 || spare != 0) {
        return DecodeError::BadPayloadField;
    }
    hk.mode = static_cast<Mode>(mode);
    hk.detector_temp_c = temp / 100.0;
    hk.bus_voltage_v = voltage / 1000.0;
    hk.current_a = current / 1000.0;
    hk.azimuth_deg = az / 1000.0;
    hk.elevation_deg = el / 1000.0;
    hk.cooler_ready = (flags & kCoolerReady) != 0;
    hk.gimbal_moving = (flags & kGimbalMoving) != 0;
    hk.bit_fault = (flags & kBitFault) != 0;
    return hk;
}

std::vector<std::uint8_t> pack(const Event& event) {
    if (event.severity > 3) throw std::out_of_range("severity must be 0-3");
    if (event.text.size() > kMaxEventText) throw std::out_of_range("event text longer than 255 bytes");
    std::vector<std::uint8_t> out;
    ByteWriter w(out);
    w.u8(event.severity);
    w.u8(static_cast<std::uint8_t>(event.text.size()));
    w.bytes(reinterpret_cast<const std::uint8_t*>(event.text.data()), event.text.size());
    return out;
}

Result<Event> unpack_event(const std::vector<std::uint8_t>& user_data) {
    if (user_data.size() < 2 || user_data.size() != 2u + user_data[1]) return DecodeError::BadPayloadSize;
    Event event;
    event.severity = user_data[0];
    if (event.severity > 3) return DecodeError::BadPayloadField;
    for (std::size_t i = 2; i < user_data.size(); ++i) {
        if (user_data[i] < 0x20 || user_data[i] > 0x7E) return DecodeError::BadPayloadField;  // printable ASCII
        event.text.push_back(static_cast<char>(user_data[i]));
    }
    return event;
}

}  // namespace tlm
