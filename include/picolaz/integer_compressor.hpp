#pragma once

#include "arithmetic_codec.hpp"
#include <memory>
#include <vector>

namespace picolaz {

class IntegerCompressor {
public:
    IntegerCompressor(ArithmeticEncoder* enc, std::uint32_t bits = 16, std::uint32_t contexts = 1,
                      std::uint32_t bits_high = 8, std::uint32_t range = 0);
    IntegerCompressor(ArithmeticDecoder* dec, std::uint32_t bits = 16, std::uint32_t contexts = 1,
                      std::uint32_t bits_high = 8, std::uint32_t range = 0);

    void initCompressor();
    void initDecompressor();

    void compress(std::int32_t pred, std::int32_t real, std::uint32_t context = 0);
    std::int32_t  decompress(std::int32_t pred, std::uint32_t context = 0);

    std::uint32_t getK() const { return k_; }

private:
    void writeCorrector(std::int32_t c, ArithmeticModel& model);
    std::int32_t  readCorrector(ArithmeticModel& model);

    ArithmeticEncoder* enc_{nullptr};
    ArithmeticDecoder* dec_{nullptr};

    std::uint32_t bits_;
    std::uint32_t contexts_;
    std::uint32_t bits_high_;
    std::uint32_t range_;

    std::uint32_t corr_bits_;
    std::uint32_t corr_range_;
    std::int32_t corr_min_;
    std::int32_t corr_max_;

    std::uint32_t k_{0};

    std::vector<ArithmeticModel> m_bits_;
    ArithmeticBitModel m_corrector_0_;
    std::vector<ArithmeticModel> m_corrector_;
};

} // namespace picolaz
