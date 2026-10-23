#include "tlm/crc16.hpp"

#include <array>

namespace tlm {
namespace {

// table built at compile time: one lookup per byte instead of 8 shifts
constexpr std::array<std::uint16_t, 256> make_table() {
    std::array<std::uint16_t, 256> table{};
    for (std::uint32_t byte = 0; byte < 256; ++byte) {
        std::uint32_t crc = byte << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
        }
        table[byte] = static_cast<std::uint16_t>(crc & 0xFFFFu);
    }
    return table;
}

constexpr auto kTable = make_table();

}  // namespace

std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t size, std::uint16_t crc) {
    for (std::size_t i = 0; i < size; ++i) {
        const auto index = static_cast<std::uint8_t>((crc >> 8) ^ data[i]);
        crc = static_cast<std::uint16_t>((crc << 8) ^ kTable[index]);
    }
    return crc;
}

}  // namespace tlm
