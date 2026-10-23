#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "tlm/crc16.hpp"

namespace {

std::uint16_t crc_of(const std::string& text) {
    return tlm::crc16_ccitt(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

}  // namespace

TEST(Crc16, StandardCheckValue) {
    // The catalogued check value for CRC-16/CCITT-FALSE.
    EXPECT_EQ(crc_of("123456789"), 0x29B1);
}

TEST(Crc16, EmptyInputIsInitialValue) {
    EXPECT_EQ(crc_of(""), 0xFFFF);
}

TEST(Crc16, IncrementalMatchesOneShot) {
    const std::string text = "CCSDS space packet protocol";
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    const std::uint16_t partial = tlm::crc16_ccitt(bytes, 10);
    EXPECT_EQ(tlm::crc16_ccitt(bytes + 10, text.size() - 10, partial), crc_of(text));
}

TEST(Crc16, DetectsEverySingleBitError) {
    std::string text = "telemetry frame payload";
    const std::uint16_t good = crc_of(text);
    for (std::size_t byte = 0; byte < text.size(); ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            std::string damaged = text;
            damaged[byte] = static_cast<char>(damaged[byte] ^ (1 << bit));
            EXPECT_NE(crc_of(damaged), good) << "byte " << byte << " bit " << bit;
        }
    }
}
