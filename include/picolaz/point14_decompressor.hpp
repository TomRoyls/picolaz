#pragma once

#include "compressor_types.hpp"
#include "streams.hpp"
#include <array>
#include <memory>
#include <vector>

namespace picolaz {

class Point14Decompressor {
public:
    Point14Decompressor();

    void chunk_sizes(ByteStreamIn* in);
    void init(ByteStreamIn* in);
    void read(std::uint8_t* raw_bytes);

private:
    void init_context(std::uint32_t sc);
    void read_gpstime(PackedPoint14* point, LAScontextPOINT14& ctx);

    std::vector<std::uint8_t> layer_data_;
    std::array<std::uint32_t, POINT14_NUM_LAYERS> layer_sizes_{};
    std::array<ByteStreamIn, POINT14_NUM_LAYERS> layer_in_;
    std::array<ArithmeticDecoder, POINT14_NUM_LAYERS> layer_dec_;

    LAScontextPOINT14 contexts_[4];
    std::uint32_t current_context_{0};
    bool first_point_{true};
};

} // namespace picolaz
