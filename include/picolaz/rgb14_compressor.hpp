#pragma once

#include "compressor_types.hpp"
#include "streams.hpp"

namespace picolaz {

class RGB14Compressor {
public:
    RGB14Compressor();

    void init(ByteStreamOut* main_out);
    void write(const std::uint16_t* rgb);
    void chunk_sizes();
    void chunk_bytes();

private:
    void init_context(std::uint32_t sc);

    ByteStreamOut* main_out_{nullptr};
    ByteStreamOut layer_out_;
    ArithmeticEncoder layer_enc_;

    LAScontextRGB14 contexts_[4];
    std::uint32_t current_context_{0};
    bool first_point_{true};
};

} // namespace picolaz
