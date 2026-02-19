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
    explicit ByteStreamIn(const u8* data, u32 size)
        : data_(data), size_(size), pos_(0) {}

    void init(const u8* data, u32 size) {
        data_ = data;
        size_ = size;
        pos_ = 0;
    }

    u8 get_byte() {
        if (pos_ >= size_) return 0;
        return data_[pos_++];
    }

    void get_bytes(u8* dst, u32 n) {
        if (pos_ + n > size_) {
            u32 avail = (pos_ < size_) ? size_ - pos_ : 0;
            if (avail) std::memcpy(dst, data_ + pos_, avail);
            std::memset(dst + avail, 0, n - avail);
            pos_ = size_;
            return;
        }
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
    }

    void skip_bytes(u32 n) { pos_ += n; }

    u32 position() const { return pos_; }
    u32 remaining() const { return pos_ < size_ ? size_ - pos_ : 0; }

    void get_16bits_le(u8* dst) { get_bytes(dst, 2); }
    void get_32bits_le(u8* dst) { get_bytes(dst, 4); }
    void get_64bits_le(u8* dst) { get_bytes(dst, 8); }

private:
    const u8* data_{nullptr};
    u32 size_{0};
    u32 pos_{0};
};

// ─── Byte stream output (writes to growable buffer) ─────────────────
class ByteStreamOut {
public:
    ByteStreamOut() { buf_.reserve(4096); }

    void put_byte(u8 b) { buf_.push_back(b); }

    void put_bytes(const u8* src, u32 n) {
        buf_.insert(buf_.end(), src, src + n);
    }

    void put_16bits_le(const u8* src) { put_bytes(src, 2); }
    void put_32bits_le(const u8* src) { put_bytes(src, 4); }
    void put_64bits_le(const u8* src) { put_bytes(src, 8); }

    const u8* data() const { return buf_.data(); }
    u32 size() const { return static_cast<u32>(buf_.size()); }

    void seek(u32 pos) { buf_.resize(pos); }
    u32 position() const { return static_cast<u32>(buf_.size()); }

    void clear() { buf_.clear(); }

    std::vector<u8>& buffer() { return buf_; }
    const std::vector<u8>& buffer() const { return buf_; }

private:
    std::vector<u8> buf_;
};

} // namespace picolaz
