#pragma once

#include "compressor_types.hpp"
#include "streams.hpp"

namespace picolaz {

class RGBNIR14Compressor {
public:
    RGBNIR14Compressor();

    void init(ByteStreamOut* main_out);
    void write(const std::uint16_t* rgbnir);
    void chunk_sizes();
    void chunk_bytes();

private:
    void init_context(std::uint32_t sc);

    ByteStreamOut* main_out_{nullptr};
    ByteStreamOut rgb_out_;
    ByteStreamOut nir_out_;
    ArithmeticEncoder rgb_enc_;
    ArithmeticEncoder nir_enc_;

    LAScontextRGBNIR14 contexts_[4];
    std::uint32_t current_context_{0};
    bool first_point_{true};
};

} // namespace picolaz
