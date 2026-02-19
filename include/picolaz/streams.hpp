#pragma once

#include "types.hpp"
#include <vector>
#include <cstring>
#include <cassert>

namespace picolaz {

// ─── Byte stream input (reads from memory buffer) ───────────────────
class ByteStreamIn {
public:
    ByteStreamIn() = default;
    explicit ByteStreamIn(const std::uint8_t* data, std::uint32_t size)
        : data_(data), size_(size), pos_(0) {}

    void init(const std::uint8_t* data, std::uint32_t size) {
        data_ = data;
        size_ = size;
        pos_ = 0;
    }

    std::uint8_t get_byte() {
        if (pos_ >= size_) return 0;
        return data_[pos_++];
    }

    void get_bytes(std::uint8_t* dst, std::uint32_t n) {
        if (pos_ + n > size_) {
            std::uint32_t avail = (pos_ < size_) ? size_ - pos_ : 0;
            if (avail) std::memcpy(dst, data_ + pos_, avail);
            std::memset(dst + avail, 0, n - avail);
            pos_ = size_;
            return;
        }
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
    }

    void skip_bytes(std::uint32_t n) { pos_ += n; }

    std::uint32_t position() const { return pos_; }
    std::uint32_t remaining() const { return pos_ < size_ ? size_ - pos_ : 0; }

    void get_16bits_le(std::uint8_t* dst) { get_bytes(dst, 2); }
    void get_32bits_le(std::uint8_t* dst) { get_bytes(dst, 4); }
    void get_64bits_le(std::uint8_t* dst) { get_bytes(dst, 8); }

private:
    const std::uint8_t* data_{nullptr};
    std::uint32_t size_{0};
    std::uint32_t pos_{0};
};

// ─── Byte stream output (writes to growable buffer) ─────────────────
class ByteStreamOut {
public:
    ByteStreamOut() { buf_.reserve(4096); }

    void put_byte(std::uint8_t b) { buf_.push_back(b); }

    void put_bytes(const std::uint8_t* src, std::uint32_t n) {
        buf_.insert(buf_.end(), src, src + n);
    }

    void put_16bits_le(const std::uint8_t* src) { put_bytes(src, 2); }
    void put_32bits_le(const std::uint8_t* src) { put_bytes(src, 4); }
    void put_64bits_le(const std::uint8_t* src) { put_bytes(src, 8); }

    const std::uint8_t* data() const { return buf_.data(); }
    std::uint32_t size() const { return static_cast<std::uint32_t>(buf_.size()); }

    void seek(std::uint32_t pos) { buf_.resize(pos); }
    std::uint32_t position() const { return static_cast<std::uint32_t>(buf_.size()); }

    void clear() { buf_.clear(); }

    std::vector<std::uint8_t>& buffer() { return buf_; }
    const std::vector<std::uint8_t>& buffer() const { return buf_; }

private:
    std::vector<std::uint8_t> buf_;
};

} // namespace picolaz
