// Hostile-input tests that run in every build. The libFuzzer target in
// fuzz/ goes much further; these keep a baseline in the normal test run, and
// under the sanitizer build any out-of-bounds read or UB aborts the test.

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "tlm/payloads.hpp"
#include "tlm/stream.hpp"

TEST(Robustness, RandomBuffersNeverDecodeOrCrash) {
    std::mt19937 rng(7);
    int accepted = 0;
    for (int i = 0; i < 50000; ++i) {
        std::vector<std::uint8_t> buffer(rng() % 80);
        for (auto& b : buffer) b = static_cast<std::uint8_t>(rng());
        const auto result = tlm::decode(buffer.data(), buffer.size());
        if (result.ok()) ++accepted;
    }
    // a random buffer must also pass the length and CRC checks: about 1 in 16
    // million for a matching length, then 1 in 65536 for the CRC
    EXPECT_EQ(accepted, 0);
}

TEST(Robustness, MutatedValidPacketsHandledSafely) {
    tlm::Housekeeping hk;
    hk.mode = tlm::Mode::Ready;
    const auto good = tlm::encode(tlm::kHousekeepingApid, 9, {}, tlm::pack(hk));
    std::mt19937 rng(11);
    for (int i = 0; i < 20000; ++i) {
        auto packet = good;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int f = 0; f < flips; ++f) packet[rng() % packet.size()] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
        if (rng() % 4 == 0) packet.resize(rng() % (packet.size() + 8));
        const auto result = tlm::decode(packet.data(), packet.size());
        if (result.ok()) {
            // only possible if the mutation was undone or hit nothing checkable
            (void)tlm::unpack_housekeeping(result.value().user_data);
        }
    }
    SUCCEED();
}

TEST(Robustness, DepacketizerSurvivesRandomStream) {
    std::mt19937 rng(3);
    tlm::Depacketizer d;
    std::vector<std::uint8_t> chunk(512);
    for (int i = 0; i < 2000; ++i) {
        for (auto& b : chunk) b = static_cast<std::uint8_t>(rng());
        if (i % 50 == 0) {  // sprinkle real markers to exercise the length path
            chunk[0] = 0x1A; chunk[1] = 0xCF; chunk[2] = 0xFC; chunk[3] = 0x1D;
        }
        d.push(chunk.data(), chunk.size());
    }
    EXPECT_LE(d.buffered(), tlm::kMaxPacketSize + 4);  // memory stays bounded
}
