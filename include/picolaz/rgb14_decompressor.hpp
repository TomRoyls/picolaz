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
    void read(std::uint16_t* rgb);

private:
    void init_context(std::uint32_t sc);

    std::vector<std::uint8_t> layer_data_;
    std::uint32_t layer_size_{0};
    ByteStreamIn layer_in_;
    ArithmeticDecoder layer_dec_;

    LAScontextRGB14 contexts_[4];
    std::uint32_t current_context_{0};
    bool first_point_{true};
};

} // namespace picolaz
