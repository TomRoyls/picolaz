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
    void read(u8* raw_bytes);

private:
    void init_context(u32 sc);
    void read_gpstime(PackedPoint14* point, LAScontextPOINT14& ctx);

    std::vector<u8> layer_data_;
    std::array<u32, POINT14_NUM_LAYERS> layer_sizes_{};
    std::array<ByteStreamIn, POINT14_NUM_LAYERS> layer_in_;
    std::array<ArithmeticDecoder, POINT14_NUM_LAYERS> layer_dec_;

    LAScontextPOINT14 contexts_[4];
    u32 current_context_{0};
    bool first_point_{true};
};

} // namespace picolaz
