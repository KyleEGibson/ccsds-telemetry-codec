#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "tlm/stream.hpp"

namespace {

std::vector<std::uint8_t> framed(std::uint16_t seq, std::size_t payload = 8) {
    return tlm::frame(tlm::encode(0x101, seq, tlm::Timestamp{100u + seq, 0}, std::vector<std::uint8_t>(payload, 0x42)));
}

void append(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& more) {
    out.insert(out.end(), more.begin(), more.end());
}

std::vector<std::uint16_t> sequences(const std::vector<tlm::StreamItem>& items) {
    std::vector<std::uint16_t> out;
    for (const auto& item : items) {
        if (item.packet.ok()) out.push_back(item.packet.value().header.sequence_count);
    }
    return out;
}

}  // namespace

TEST(Depacketizer, SeveralPacketsInOneRead) {
    std::vector<std::uint8_t> stream;
    for (std::uint16_t s = 0; s < 5; ++s) append(stream, framed(s));
    tlm::Depacketizer d;
    const auto items = d.push(stream.data(), stream.size());
    EXPECT_EQ(sequences(items), (std::vector<std::uint16_t>{0, 1, 2, 3, 4}));
    EXPECT_EQ(d.buffered(), 0u);
    EXPECT_EQ(d.total_skipped(), 0u);
}

TEST(Depacketizer, PacketSplitAcrossReadsByteByByte) {
    const auto stream = framed(7, 40);
    tlm::Depacketizer d;
    std::vector<tlm::StreamItem> items;
    for (std::uint8_t byte : stream) {
        for (auto& item : d.push(&byte, 1)) items.push_back(std::move(item));
    }
    EXPECT_EQ(sequences(items), (std::vector<std::uint16_t>{7}));
}

TEST(Depacketizer, SkipsGarbageAndResyncs) {
    std::vector<std::uint8_t> stream = {0x00, 0xFF, 0x1A, 0xCF};  // noise, including half a marker
    append(stream, framed(1));
    append(stream, {0x55, 0x55, 0x55});
    append(stream, framed(2));
    tlm::Depacketizer d;
    const auto items = d.push(stream.data(), stream.size());
    EXPECT_EQ(sequences(items), (std::vector<std::uint16_t>{1, 2}));
    EXPECT_EQ(items[0].skipped_before, 4u);
    EXPECT_EQ(items[1].skipped_before, 3u);
}

TEST(Depacketizer, CorruptPacketReportedThenRecovers) {
    std::vector<std::uint8_t> stream = framed(1);
    auto bad = framed(2);
    bad[15] ^= 0xFF;  // damage the payload
    append(stream, bad);
    append(stream, framed(3));
    tlm::Depacketizer d;
    const auto items = d.push(stream.data(), stream.size());
    ASSERT_EQ(items.size(), 3u);
    EXPECT_TRUE(items[0].packet.ok());
    EXPECT_EQ(items[1].packet.error(), tlm::DecodeError::CrcMismatch);
    EXPECT_EQ(sequences(items), (std::vector<std::uint16_t>{1, 3}));
}

TEST(Depacketizer, DamagedLengthFieldDoesNotSwallowFollowingPackets) {
    std::vector<std::uint8_t> stream = framed(1);
    auto bad = framed(2);
    bad[8] = 0xFF;  // length field now claims ~65 KB
    bad[9] = 0xFF;
    append(stream, bad);
    for (std::uint16_t s = 3; s < 6; ++s) append(stream, framed(s));
    tlm::Depacketizer d;
    auto items = d.push(stream.data(), stream.size());
    // the bogus length makes the depacketizer wait; more traffic must not be lost
    std::vector<std::uint8_t> more;
    for (std::uint16_t s = 6; s < 9; ++s) append(more, framed(s));
    // Note: the damaged packet holds ~65 KB of buffer hostage until enough
    // data arrives to prove its CRC wrong; see docs/packet_format.md.
    for (int i = 0; i < 2000; ++i) {
        for (auto& item : d.push(more.data(), more.size())) items.push_back(std::move(item));
    }
    const auto seqs = sequences(items);
    ASSERT_GE(seqs.size(), 4u);
    EXPECT_EQ(seqs[0], 1);
    EXPECT_EQ(seqs[1], 3);  // resynced right after the damaged packet
}

TEST(Depacketizer, RandomChunkingGivesSameResult) {
    std::vector<std::uint8_t> stream;
    std::mt19937 rng(42);
    for (std::uint16_t s = 0; s < 200; ++s) append(stream, framed(s, rng() % 64));
    tlm::Depacketizer d;
    std::vector<tlm::StreamItem> items;
    std::size_t pos = 0;
    while (pos < stream.size()) {
        const std::size_t n = std::min<std::size_t>(1 + rng() % 97, stream.size() - pos);
        for (auto& item : d.push(stream.data() + pos, n)) items.push_back(std::move(item));
        pos += n;
    }
    ASSERT_EQ(items.size(), 200u);
    for (std::uint16_t s = 0; s < 200; ++s) EXPECT_EQ(items[s].packet.value().header.sequence_count, s);
}
