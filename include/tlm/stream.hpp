// Pulls packets out of a byte stream (serial, TCP, a recording). Each packet
// is preceded by the sync marker 1ACFFC1D; after garbage or a bad packet we
// resync on the next marker.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tlm/space_packet.hpp"

namespace tlm {

constexpr std::uint8_t kSyncMarker[4] = {0x1A, 0xCF, 0xFC, 0x1D};

struct StreamItem {
    Result<SpacePacket> packet;
    std::size_t skipped_before = 0;  // bytes discarded while searching for sync
};

class Depacketizer {
public:
    // Feed any number of bytes; returns every packet (or decode error) completed by them.
    std::vector<StreamItem> push(const std::uint8_t* data, std::size_t size);

    std::size_t buffered() const { return buffer_.size(); }
    std::size_t total_skipped() const { return total_skipped_; }

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t pending_skip_ = 0;
    std::size_t total_skipped_ = 0;
};

// Prefix a packet with the sync marker for transmission on a stream.
std::vector<std::uint8_t> frame(const std::vector<std::uint8_t>& packet);

}  // namespace tlm
