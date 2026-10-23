// CCSDS Space Packet (CCSDS 133.0-B-2) with a CUC time secondary header and
// a CRC-16 Packet Error Control field. Layout: docs/packet_format.md.
//
//  +----------------------- primary header (6 bytes) ------------------------+
//  | ver(3) type(1) shdr(1) APID(11) | seqflags(2) seqcount(14) | length(16)  |
//  +------------------------------- data field ------------------------------+
//  | time: coarse s (4) fine (2) | user data (0..65520) | CRC-16 (2)          |
//  +-------------------------------------------------------------------------+
//  length = (size of data field in bytes) - 1
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace tlm {

constexpr std::size_t kPrimaryHeaderSize = 6;
constexpr std::size_t kSecondaryHeaderSize = 6;
constexpr std::size_t kCrcSize = 2;
constexpr std::size_t kMinPacketSize = kPrimaryHeaderSize + kSecondaryHeaderSize + kCrcSize;
constexpr std::size_t kMaxDataFieldSize = 65536;
constexpr std::size_t kMaxPacketSize = kPrimaryHeaderSize + kMaxDataFieldSize;
constexpr std::size_t kMaxUserDataSize = kMaxDataFieldSize - kSecondaryHeaderSize - kCrcSize;
constexpr std::uint16_t kMaxApid = 0x7FF;
constexpr std::uint16_t kSequenceModulo = 16384;  // 14-bit counter

enum class PacketType : std::uint8_t { Telemetry = 0, Telecommand = 1 };
enum class SequenceFlags : std::uint8_t { Continuation = 0, First = 1, Last = 2, Unsegmented = 3 };

struct PrimaryHeader {
    std::uint8_t version = 0;
    PacketType type = PacketType::Telemetry;
    bool secondary_header = true;
    std::uint16_t apid = 0;
    SequenceFlags sequence_flags = SequenceFlags::Unsegmented;
    std::uint16_t sequence_count = 0;
    std::uint16_t data_length = 0;  // data field size - 1, as transmitted
};

// CCSDS Unsegmented time Code: whole seconds since the mission epoch plus
// 1/65536-second fractions.
struct Timestamp {
    std::uint32_t coarse = 0;
    std::uint16_t fine = 0;

    double seconds() const { return coarse + fine / 65536.0; }
    static Timestamp from_seconds(double seconds);
};

struct SpacePacket {
    PrimaryHeader header;
    Timestamp time;
    std::vector<std::uint8_t> user_data;
};

enum class DecodeError {
    TooShort,           // fewer bytes than the smallest valid packet
    BadVersion,         // version field is not 0
    NotTelemetry,       // type bit says telecommand
    NoSecondaryHeader,  // this mission requires the time header
    LengthMismatch,     // buffer size disagrees with the length field
    CrcMismatch,        // packet error control failed: corrupted in transit
    BadPayloadSize,     // user data is the wrong size for its APID
    BadPayloadField,    // a field holds a value the format does not allow
};

const char* to_string(DecodeError error);

// Value or error. Decode doesn't throw since corrupt input is normal on a link.
template <class T>
class Result {
public:
    Result(T value) : state_(std::move(value)) {}            // NOLINT(google-explicit-constructor)
    Result(DecodeError error) : state_(error) {}             // NOLINT(google-explicit-constructor)

    bool ok() const { return std::holds_alternative<T>(state_); }
    const T& value() const { return std::get<T>(state_); }
    DecodeError error() const { return std::get<DecodeError>(state_); }

private:
    std::variant<T, DecodeError> state_;
};

// Throws std::invalid_argument if apid/count/size don't fit (a sender bug).
std::vector<std::uint8_t> encode(std::uint16_t apid, std::uint16_t sequence_count, Timestamp time,
                                 const std::vector<std::uint8_t>& user_data);

// Decode exactly one packet occupying the whole buffer.
Result<SpacePacket> decode(const std::uint8_t* data, std::size_t size);

// Total packet size announced by a primary header, or 0 if fewer than 6 bytes.
std::size_t packet_size(const std::uint8_t* data, std::size_t size);

}  // namespace tlm
