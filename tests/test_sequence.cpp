#include <gtest/gtest.h>

#include "tlm/sequence.hpp"

using tlm::SequenceStatus;

TEST(SequenceMonitor, InOrder) {
    tlm::SequenceMonitor m;
    EXPECT_EQ(m.observe(1, 10).status, SequenceStatus::First);
    EXPECT_EQ(m.observe(1, 11).status, SequenceStatus::InOrder);
    EXPECT_EQ(m.observe(1, 12).status, SequenceStatus::InOrder);
    EXPECT_EQ(m.stats().at(1).lost, 0u);
}

TEST(SequenceMonitor, GapCountsLostPackets) {
    tlm::SequenceMonitor m;
    m.observe(1, 10);
    const auto event = m.observe(1, 14);
    EXPECT_EQ(event.status, SequenceStatus::Gap);
    EXPECT_EQ(event.expected, 11);
    EXPECT_EQ(event.lost, 3);
    EXPECT_EQ(m.stats().at(1).lost, 3u);
}

TEST(SequenceMonitor, Duplicate) {
    tlm::SequenceMonitor m;
    m.observe(1, 10);
    EXPECT_EQ(m.observe(1, 10).status, SequenceStatus::Duplicate);
    EXPECT_EQ(m.observe(1, 11).status, SequenceStatus::InOrder);  // duplicate did not move the counter
    EXPECT_EQ(m.stats().at(1).duplicates, 1u);
}

TEST(SequenceMonitor, LateOlderPacketIsOutOfOrder) {
    tlm::SequenceMonitor m;
    m.observe(1, 10);
    m.observe(1, 12);                                               // 11 missing so far
    EXPECT_EQ(m.observe(1, 11).status, SequenceStatus::OutOfOrder);  // arrives late
    EXPECT_EQ(m.observe(1, 13).status, SequenceStatus::InOrder);
}

TEST(SequenceMonitor, WrapsAround) {
    tlm::SequenceMonitor m;
    m.observe(1, 16382);
    EXPECT_EQ(m.observe(1, 16383).status, SequenceStatus::InOrder);
    EXPECT_EQ(m.observe(1, 0).status, SequenceStatus::InOrder);
    const auto gap = m.observe(1, 3);
    EXPECT_EQ(gap.status, SequenceStatus::Gap);
    EXPECT_EQ(gap.lost, 2);
}

TEST(SequenceMonitor, GapAcrossWrap) {
    tlm::SequenceMonitor m;
    m.observe(1, 16380);
    const auto event = m.observe(1, 2);
    EXPECT_EQ(event.status, SequenceStatus::Gap);
    EXPECT_EQ(event.lost, 5);  // 16381, 16382, 16383, 0, 1
}

TEST(SequenceMonitor, ApidsAreIndependent) {
    tlm::SequenceMonitor m;
    m.observe(1, 100);
    m.observe(2, 7);
    EXPECT_EQ(m.observe(1, 101).status, SequenceStatus::InOrder);
    EXPECT_EQ(m.observe(2, 8).status, SequenceStatus::InOrder);
}
