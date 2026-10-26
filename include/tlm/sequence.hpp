// Per-APID packet sequence tracking: detects lost, duplicated and
// out-of-order packets from the 14-bit sequence count, including wraparound
// (16383 -> 0).
#pragma once

#include <cstdint>
#include <map>

namespace tlm {

enum class SequenceStatus { First, InOrder, Gap, Duplicate, OutOfOrder };

const char* to_string(SequenceStatus status);

struct SequenceEvent {
    SequenceStatus status = SequenceStatus::First;
    std::uint16_t expected = 0;  // the count that would have been in order
    std::uint16_t received = 0;
    std::uint16_t lost = 0;      // packets missing before this one (Gap only)
};

struct SequenceStats {
    std::uint64_t received = 0;
    std::uint64_t lost = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t out_of_order = 0;
};

class SequenceMonitor {
public:
    SequenceEvent observe(std::uint16_t apid, std::uint16_t sequence_count);
    const std::map<std::uint16_t, SequenceStats>& stats() const { return stats_; }

private:
    std::map<std::uint16_t, std::uint16_t> last_;
    std::map<std::uint16_t, SequenceStats> stats_;
};

}  // namespace tlm
