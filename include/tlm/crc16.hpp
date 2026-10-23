// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, no xorout), the
// Packet Error Control used by ECSS PUS. crc16_ccitt("123456789") == 0x29B1
#pragma once

#include <cstddef>
#include <cstdint>

namespace tlm {

constexpr std::uint16_t kCrcInit = 0xFFFF;

// Pass the previous result as `crc` to checksum data in pieces.
std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t size, std::uint16_t crc = kCrcInit);

}  // namespace tlm
