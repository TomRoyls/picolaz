#pragma once

#include "compressor_types.hpp"
#include "streams.hpp"
#include <vector>

namespace picolaz {

class RGB14Decompressor {
public:
    RGB14Decompressor();

    void chunk_sizes(ByteStreamIn* in);
    void init(ByteStreamIn* in);
    void read(u16* rgb);

private:
    void init_context(u32 sc);

    std::vector<u8> layer_data_;
    u32 layer_size_{0};
    ByteStreamIn layer_in_;
    ArithmeticDecoder layer_dec_;

    LAScontextRGB14 contexts_[4];
    u32 current_context_{0};
    bool first_point_{true};
};

} // namespace picolaz
