#include "tlm/sequence.hpp"

#include "tlm/space_packet.hpp"

namespace tlm {

const char* to_string(SequenceStatus status) {
    switch (status) {
        case SequenceStatus::First: return "first";
        case SequenceStatus::InOrder: return "in_order";
        case SequenceStatus::Gap: return "gap";
        case SequenceStatus::Duplicate: return "duplicate";
        case SequenceStatus::OutOfOrder: return "out_of_order";
    }
    return "unknown";
}

SequenceEvent SequenceMonitor::observe(std::uint16_t apid, std::uint16_t sequence_count) {
    SequenceStats& stats = stats_[apid];
    ++stats.received;
    SequenceEvent event;
    event.received = sequence_count;

    const auto found = last_.find(apid);
    if (found == last_.end()) {
        last_[apid] = sequence_count;
        event.status = SequenceStatus::First;
        event.expected = sequence_count;
        return event;
    }

    const std::uint16_t last = found->second;
    event.expected = static_cast<std::uint16_t>((last + 1) % kSequenceModulo);
    // distance ahead of the last count, mod 2^14. Less than half the range
    // ahead counts as newer, otherwise it's an old packet showing up late.
    const auto delta = static_cast<std::uint16_t>((sequence_count + kSequenceModulo - last) % kSequenceModulo);

    if (delta == 1) {
        event.status = SequenceStatus::InOrder;
    } else if (delta == 0) {
        event.status = SequenceStatus::Duplicate;
        ++stats.duplicates;
        return event;  // do not advance
    } else if (delta < kSequenceModulo / 2) {
        event.status = SequenceStatus::Gap;
        event.lost = static_cast<std::uint16_t>(delta - 1);
        stats.lost += event.lost;
    } else {
        event.status = SequenceStatus::OutOfOrder;  // an older packet arriving late
        ++stats.out_of_order;
        return event;  // do not move backwards
    }
    found->second = sequence_count;
    return event;
}

}  // namespace tlm
