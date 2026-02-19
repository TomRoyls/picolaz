#pragma once

#include "compressor_types.hpp"
#include "streams.hpp"
#include <array>
#include <memory>

namespace picolaz {

class Point14Compressor {
public:
    Point14Compressor();

    void init(ByteStreamOut* main_out);
    void write(const u8* raw_bytes);
    void chunk_sizes();
    void chunk_bytes();

private:
    void init_context(u32 sc);
    void write_gpstime(const PackedPoint14* point, LAScontextPOINT14& ctx);

    ByteStreamOut* main_out_{nullptr};
    std::array<ByteStreamOut, POINT14_NUM_LAYERS> layer_out_;
    std::array<ArithmeticEncoder, POINT14_NUM_LAYERS> layer_enc_;

    LAScontextPOINT14 contexts_[4];
    u32 current_context_{0};
    bool first_point_{true};
};

} // namespace picolaz
