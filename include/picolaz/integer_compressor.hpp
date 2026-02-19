#pragma once

#include "arithmetic_codec.hpp"
#include <memory>
#include <vector>

namespace picolaz {

class IntegerCompressor {
public:
    IntegerCompressor(ArithmeticEncoder* enc, u32 bits = 16, u32 contexts = 1,
                      u32 bits_high = 8, u32 range = 0);
    IntegerCompressor(ArithmeticDecoder* dec, u32 bits = 16, u32 contexts = 1,
                      u32 bits_high = 8, u32 range = 0);

    void initCompressor();
    void initDecompressor();

    void compress(i32 pred, i32 real, u32 context = 0);
    i32  decompress(i32 pred, u32 context = 0);

    u32 getK() const { return k_; }

private:
    void writeCorrector(i32 c, ArithmeticModel& model);
    i32  readCorrector(ArithmeticModel& model);

    ArithmeticEncoder* enc_{nullptr};
    ArithmeticDecoder* dec_{nullptr};

    u32 bits_;
    u32 contexts_;
    u32 bits_high_;
    u32 range_;

    u32 corr_bits_;
    u32 corr_range_;
    i32 corr_min_;
    i32 corr_max_;

    u32 k_{0};

    std::vector<ArithmeticModel> m_bits_;
    ArithmeticBitModel m_corrector_0_;
    std::vector<ArithmeticModel> m_corrector_;
};

} // namespace picolaz
