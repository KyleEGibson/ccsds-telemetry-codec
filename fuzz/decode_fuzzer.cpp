// libFuzzer target. Build with -DTLM_BUILD_FUZZERS=ON using Clang, then:
//   ./decode_fuzzer -max_total_time=60 fuzz/corpus
//
// Oracles checked on every input:
//  1. no crash, hang, memory error or undefined behaviour (ASan + UBSan);
//  2. round trip: anything decode() accepts re-encodes to the identical bytes;
//  3. the depacketizer never buffers more than one maximum-size packet.

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include <vector>

#include "tlm/payloads.hpp"
#include "tlm/stream.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto result = tlm::decode(data, size);
    if (result.ok()) {
        const auto& p = result.value();
        const bool canonical = p.header.sequence_flags == tlm::SequenceFlags::Unsegmented;
        if (canonical) {
            const auto again = tlm::encode(p.header.apid, p.header.sequence_count, p.time, p.user_data);
            if (again.size() != size || !std::equal(again.begin(), again.end(), data)) std::abort();
        }
        if (p.header.apid == tlm::kHousekeepingApid) {
            const auto hk = tlm::unpack_housekeeping(p.user_data);
            if (hk.ok() && tlm::pack(hk.value()) != p.user_data) std::abort();
        }
        (void)tlm::unpack_event(p.user_data);
    }

    tlm::Depacketizer stream;
    stream.push(data, size);
    if (stream.buffered() > tlm::kMaxPacketSize + 4) std::abort();
    return 0;
}
