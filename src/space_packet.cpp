#include "tlm/space_packet.hpp"

#include <cmath>
#include <stdexcept>

#include "tlm/byteio.hpp"
#include "tlm/crc16.hpp"

namespace tlm {

const char* to_string(DecodeError error) {
    switch (error) {
        case DecodeError::TooShort: return "too_short";
        case DecodeError::BadVersion: return "bad_version";
        case DecodeError::NotTelemetry: return "not_telemetry";
        case DecodeError::NoSecondaryHeader: return "no_secondary_header";
        case DecodeError::LengthMismatch: return "length_mismatch";
        case DecodeError::CrcMismatch: return "crc_mismatch";
        case DecodeError::BadPayloadSize: return "bad_payload_size";
        case DecodeError::BadPayloadField: return "bad_payload_field";
    }
    return "unknown";
}

Timestamp Timestamp::from_seconds(double seconds) {
    if (!(seconds >= 0.0) || seconds >= 4294967296.0) {
        throw std::invalid_argument("timestamp out of range");
    }
    const double whole = std::floor(seconds);
    double fraction = std::round((seconds - whole) * 65536.0);
    auto coarse = static_cast<std::uint32_t>(whole);
    if (fraction >= 65536.0) {  // rounding carried into the next second
        fraction = 0.0;
        ++coarse;
    }
    return Timestamp{coarse, static_cast<std::uint16_t>(fraction)};
}

std::vector<std::uint8_t> encode(std::uint16_t apid, std::uint16_t sequence_count, Timestamp time,
                                 const std::vector<std::uint8_t>& user_data) {
    if (apid > kMaxApid) throw std::invalid_argument("APID exceeds 11 bits");
    if (sequence_count >= kSequenceModulo) throw std::invalid_argument("sequence count exceeds 14 bits");
    if (user_data.size() > kMaxUserDataSize) throw std::invalid_argument("user data too large");

    const std::size_t data_field = kSecondaryHeaderSize + user_data.size() + kCrcSize;
    std::vector<std::uint8_t> out;
    out.reserve(kPrimaryHeaderSize + data_field);
    ByteWriter w(out);

    // version 0, type 0 (telemetry), secondary header flag 1, then the APID
    w.u16(static_cast<std::uint16_t>((1u << 11) | apid));
    w.u16(static_cast<std::uint16_t>((static_cast<unsigned>(SequenceFlags::Unsegmented) << 14) | sequence_count));
    w.u16(static_cast<std::uint16_t>(data_field - 1));
    w.u32(time.coarse);
    w.u16(time.fine);
    w.bytes(user_data.data(), user_data.size());
    w.u16(crc16_ccitt(out.data(), out.size()));
    return out;
}

std::size_t packet_size(const std::uint8_t* data, std::size_t size) {
    if (size < kPrimaryHeaderSize) return 0;
    const std::size_t length = (static_cast<std::size_t>(data[4]) << 8) | data[5];
    return kPrimaryHeaderSize + length + 1;
}

Result<SpacePacket> decode(const std::uint8_t* data, std::size_t size) {
    if (size < kMinPacketSize) return DecodeError::TooShort;

    ByteReader r(data, size);
    std::uint16_t word1 = 0, word2 = 0;
    SpacePacket packet;
    r.u16(word1);
    r.u16(word2);
    r.u16(packet.header.data_length);

    packet.header.version = static_cast<std::uint8_t>(word1 >> 13);
    packet.header.type = static_cast<PacketType>((word1 >> 12) & 1u);
    packet.header.secondary_header = ((word1 >> 11) & 1u) != 0;
    packet.header.apid = static_cast<std::uint16_t>(word1 & kMaxApid);
    packet.header.sequence_flags = static_cast<SequenceFlags>(word2 >> 14);
    packet.header.sequence_count = static_cast<std::uint16_t>(word2 & (kSequenceModulo - 1));

    // check length + CRC before the header fields, so a flipped version bit
    // is reported as corruption rather than bad_version
    if (packet_size(data, size) != size) return DecodeError::LengthMismatch;
    const std::uint16_t expected = crc16_ccitt(data, size - kCrcSize);
    const auto received = static_cast<std::uint16_t>((data[size - 2] << 8) | data[size - 1]);
    if (expected != received) return DecodeError::CrcMismatch;

    if (packet.header.version != 0) return DecodeError::BadVersion;
    if (packet.header.type != PacketType::Telemetry) return DecodeError::NotTelemetry;
    if (!packet.header.secondary_header) return DecodeError::NoSecondaryHeader;

    r.u32(packet.time.coarse);
    r.u16(packet.time.fine);
    const std::size_t user_size = size - kMinPacketSize;
    packet.user_data.assign(data + r.position(), data + r.position() + user_size);
    return packet;
}

}  // namespace tlm
