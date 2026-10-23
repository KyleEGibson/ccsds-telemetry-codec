// Big-endian byte reader/writer. Used instead of memcpy into structs so we
// don't depend on host endianness or struct padding.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tlm {

class ByteWriter {
public:
    explicit ByteWriter(std::vector<std::uint8_t>& out) : out_(out) {}

    void u8(std::uint8_t v) { out_.push_back(v); }
    void u16(std::uint16_t v) {
        out_.push_back(static_cast<std::uint8_t>(v >> 8));
        out_.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v >> 16));
        u16(static_cast<std::uint16_t>(v & 0xFFFFu));
    }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void bytes(const std::uint8_t* data, std::size_t size) { out_.insert(out_.end(), data, data + size); }

private:
    std::vector<std::uint8_t>& out_;
};

// reads return false instead of running off the end of the buffer
class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool u8(std::uint8_t& v) {
        if (remaining() < 1) return false;
        v = data_[pos_++];
        return true;
    }
    bool u16(std::uint16_t& v) {
        if (remaining() < 2) return false;
        v = static_cast<std::uint16_t>((data_[pos_] << 8) | data_[pos_ + 1]);
        pos_ += 2;
        return true;
    }
    bool u32(std::uint32_t& v) {
        std::uint16_t hi = 0, lo = 0;
        if (remaining() < 4) return false;
        u16(hi);
        u16(lo);
        v = (static_cast<std::uint32_t>(hi) << 16) | lo;
        return true;
    }
    bool i16(std::int16_t& v) {
        std::uint16_t raw = 0;
        if (!u16(raw)) return false;
        v = static_cast<std::int16_t>(raw);
        return true;
    }
    bool i32(std::int32_t& v) {
        std::uint32_t raw = 0;
        if (!u32(raw)) return false;
        v = static_cast<std::int32_t>(raw);
        return true;
    }
    std::size_t remaining() const { return size_ - pos_; }
    std::size_t position() const { return pos_; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

}  // namespace tlm
