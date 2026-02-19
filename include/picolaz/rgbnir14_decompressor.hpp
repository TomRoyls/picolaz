#pragma once

#include "compressor_types.hpp"
#include "streams.hpp"
#include <vector>

namespace picolaz {

class RGBNIR14Decompressor {
public:
    RGBNIR14Decompressor();

    void chunk_sizes(ByteStreamIn* in);
    void init(ByteStreamIn* in);
    void read(std::uint16_t* rgbnir);

private:
    void init_context(std::uint32_t sc);

    std::vector<std::uint8_t> rgb_data_;
    std::vector<std::uint8_t> nir_data_;
    std::uint32_t rgb_size_{0};
    std::uint32_t nir_size_{0};
    ByteStreamIn rgb_in_;
    ByteStreamIn nir_in_;
    ArithmeticDecoder rgb_dec_;
    ArithmeticDecoder nir_dec_;

    LAScontextRGBNIR14 contexts_[4];
    std::uint32_t current_context_{0};
    bool first_point_{true};
};

} // namespace picolaz
