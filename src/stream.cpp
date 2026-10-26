#include "tlm/stream.hpp"

#include <algorithm>
#include <iterator>

namespace tlm {
namespace {

constexpr std::size_t kMarkerSize = sizeof(kSyncMarker);

}  // namespace

std::vector<std::uint8_t> frame(const std::vector<std::uint8_t>& packet) {
    std::vector<std::uint8_t> out(std::begin(kSyncMarker), std::end(kSyncMarker));
    out.insert(out.end(), packet.begin(), packet.end());
    return out;
}

std::vector<StreamItem> Depacketizer::push(const std::uint8_t* data, std::size_t size) {
    buffer_.insert(buffer_.end(), data, data + size);
    std::vector<StreamItem> items;

    auto discard = [this](std::size_t count) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(count));
        pending_skip_ += count;
        total_skipped_ += count;
    };

    while (true) {
        const auto marker = std::search(buffer_.begin(), buffer_.end(), std::begin(kSyncMarker), std::end(kSyncMarker));
        if (marker == buffer_.end()) {
            // keep a possible partial marker at the end, drop the rest
            if (buffer_.size() >= kMarkerSize) discard(buffer_.size() - (kMarkerSize - 1));
            break;
        }
        if (marker != buffer_.begin()) discard(static_cast<std::size_t>(marker - buffer_.begin()));

        // marker is at the front; wait for a full primary header
        if (buffer_.size() < kMarkerSize + kPrimaryHeaderSize) break;
        const std::size_t packet_bytes = packet_size(buffer_.data() + kMarkerSize, buffer_.size() - kMarkerSize);
        if (packet_bytes < kMinPacketSize || packet_bytes > kMaxPacketSize) {
            discard(1);  // a false or damaged marker: search again from the next byte
            continue;
        }
        if (buffer_.size() < kMarkerSize + packet_bytes) break;  // wait for the rest of the packet

        auto result = decode(buffer_.data() + kMarkerSize, packet_bytes);
        const bool ok = result.ok();
        items.push_back({std::move(result), pending_skip_});
        pending_skip_ = 0;
        if (ok) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(kMarkerSize + packet_bytes));
        } else {
            discard(1);  // can't trust a bad packet's length, so resync from the next byte
        }
    }
    return items;
}

}  // namespace tlm
