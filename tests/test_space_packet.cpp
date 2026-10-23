#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "tlm/crc16.hpp"
#include "tlm/space_packet.hpp"

using tlm::DecodeError;

namespace {

std::vector<std::uint8_t> sample(std::uint16_t apid = 0x101, std::uint16_t seq = 5) {
    return tlm::encode(apid, seq, tlm::Timestamp{1700, 0x8000}, {0xDE, 0xAD, 0xBE, 0xEF});
}

void refresh_crc(std::vector<std::uint8_t>& packet) {
    const auto crc = tlm::crc16_ccitt(packet.data(), packet.size() - 2);
    packet[packet.size() - 2] = static_cast<std::uint8_t>(crc >> 8);
    packet[packet.size() - 1] = static_cast<std::uint8_t>(crc & 0xFF);
}

}  // namespace

TEST(SpacePacket, PrimaryHeaderBitLayout) {
    const auto packet = sample(0x101, 5);
    // version 000, type 0, sec. header 1, APID 0x101 -> 0000 1001 0000 0001
    EXPECT_EQ(packet[0], 0x09);
    EXPECT_EQ(packet[1], 0x01);
    // sequence flags 11 (unsegmented), count 5 -> 1100 0000 0000 0101
    EXPECT_EQ(packet[2], 0xC0);
    EXPECT_EQ(packet[3], 0x05);
    // data field = 6 time + 4 user + 2 CRC = 12 bytes, so length field = 11
    EXPECT_EQ(packet[4], 0x00);
    EXPECT_EQ(packet[5], 0x0B);
    EXPECT_EQ(packet.size(), 6u + 12u);
}

TEST(SpacePacket, RoundTrip) {
    const auto result = tlm::decode(sample().data(), sample().size());
    ASSERT_TRUE(result.ok()) << tlm::to_string(result.error());
    const auto& p = result.value();
    EXPECT_EQ(p.header.apid, 0x101);
    EXPECT_EQ(p.header.sequence_count, 5);
    EXPECT_EQ(p.header.sequence_flags, tlm::SequenceFlags::Unsegmented);
    EXPECT_DOUBLE_EQ(p.time.seconds(), 1700.5);
    EXPECT_EQ(p.user_data, (std::vector<std::uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
}

TEST(SpacePacket, EmptyUserDataAndLimits) {
    const auto empty = tlm::encode(0, 0, {}, {});
    EXPECT_EQ(empty.size(), tlm::kMinPacketSize);
    EXPECT_TRUE(tlm::decode(empty.data(), empty.size()).ok());

    const auto edge = tlm::encode(tlm::kMaxApid, tlm::kSequenceModulo - 1, {}, {});
    const auto decoded = tlm::decode(edge.data(), edge.size());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().header.apid, tlm::kMaxApid);
    EXPECT_EQ(decoded.value().header.sequence_count, tlm::kSequenceModulo - 1);
}

TEST(SpacePacket, LargestPacket) {
    const std::vector<std::uint8_t> payload(tlm::kMaxUserDataSize, 0x5A);
    const auto packet = tlm::encode(1, 1, {}, payload);
    EXPECT_EQ(packet.size(), tlm::kMaxPacketSize);
    const auto decoded = tlm::decode(packet.data(), packet.size());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().user_data.size(), tlm::kMaxUserDataSize);
}

TEST(SpacePacket, EncodeRejectsUnrepresentableValues) {
    EXPECT_THROW(tlm::encode(0x800, 0, {}, {}), std::invalid_argument);
    EXPECT_THROW(tlm::encode(1, tlm::kSequenceModulo, {}, {}), std::invalid_argument);
    EXPECT_THROW(tlm::encode(1, 0, {}, std::vector<std::uint8_t>(tlm::kMaxUserDataSize + 1)), std::invalid_argument);
}

TEST(SpacePacket, TimestampConversion) {
    EXPECT_EQ(tlm::Timestamp::from_seconds(12.25).fine, 0x4000);
    const auto carry = tlm::Timestamp::from_seconds(9.9999999);  // rounds up into the next second
    EXPECT_EQ(carry.coarse, 10u);
    EXPECT_EQ(carry.fine, 0);
    EXPECT_THROW(tlm::Timestamp::from_seconds(-1.0), std::invalid_argument);
}

TEST(SpacePacketDecode, TooShort) {
    const auto packet = sample();
    EXPECT_EQ(tlm::decode(packet.data(), tlm::kMinPacketSize - 1).error(), DecodeError::TooShort);
}

TEST(SpacePacketDecode, LengthMismatch) {
    auto packet = sample();
    packet.push_back(0x00);
    EXPECT_EQ(tlm::decode(packet.data(), packet.size()).error(), DecodeError::LengthMismatch);
}

TEST(SpacePacketDecode, CrcMismatch) {
    auto packet = sample();
    packet[10] ^= 0x01;
    EXPECT_EQ(tlm::decode(packet.data(), packet.size()).error(), DecodeError::CrcMismatch);
}

TEST(SpacePacketDecode, EverySingleBitFlipIsRejected) {
    const auto good = sample();
    for (std::size_t byte = 0; byte < good.size(); ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            auto packet = good;
            packet[byte] = static_cast<std::uint8_t>(packet[byte] ^ (1u << bit));
            EXPECT_FALSE(tlm::decode(packet.data(), packet.size()).ok()) << "byte " << byte << " bit " << bit;
        }
    }
}

// The remaining header checks only apply to packets whose CRC is valid,
// i.e. built that way by a sender, not damaged in transit.
TEST(SpacePacketDecode, BadVersion) {
    auto packet = sample();
    packet[0] |= 0x20;  // version 1
    refresh_crc(packet);
    EXPECT_EQ(tlm::decode(packet.data(), packet.size()).error(), DecodeError::BadVersion);
}

TEST(SpacePacketDecode, TelecommandRejected) {
    auto packet = sample();
    packet[0] |= 0x10;
    refresh_crc(packet);
    EXPECT_EQ(tlm::decode(packet.data(), packet.size()).error(), DecodeError::NotTelemetry);
}

TEST(SpacePacketDecode, SecondaryHeaderRequired) {
    auto packet = sample();
    packet[0] &= static_cast<std::uint8_t>(~0x08u);
    refresh_crc(packet);
    EXPECT_EQ(tlm::decode(packet.data(), packet.size()).error(), DecodeError::NoSecondaryHeader);
}

TEST(SpacePacketDecode, PacketSizeFromHeader) {
    const auto packet = sample();
    EXPECT_EQ(tlm::packet_size(packet.data(), packet.size()), packet.size());
    EXPECT_EQ(tlm::packet_size(packet.data(), 5), 0u);
}
