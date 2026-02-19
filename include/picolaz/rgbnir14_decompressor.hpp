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
    void read(u16* rgbnir);

private:
    void init_context(u32 sc);

    std::vector<u8> rgb_data_;
    std::vector<u8> nir_data_;
    u32 rgb_size_{0};
    u32 nir_size_{0};
    ByteStreamIn rgb_in_;
    ByteStreamIn nir_in_;
    ArithmeticDecoder rgb_dec_;
    ArithmeticDecoder nir_dec_;

    LAScontextRGBNIR14 contexts_[4];
    u32 current_context_{0};
    bool first_point_{true};
};

} // namespace picolaz
