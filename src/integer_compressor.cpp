#include "picolaz/integer_compressor.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace picolaz {

IntegerCompressor::IntegerCompressor(ArithmeticEncoder* enc, u32 bits, u32 contexts,
                                     u32 bits_high, u32 range)
    : enc_(enc), bits_(bits), contexts_(contexts), bits_high_(std::min(bits_high, 11U)), range_(range) {
    if (range) {
        corr_bits_ = 0;
        corr_range_ = range;
        while (range) { range >>= 1; ++corr_bits_; }
        if (corr_range_ == (1U << (corr_bits_ - 1))) {
            --corr_bits_;
        }
    } else if (bits && bits < 32) {
        corr_bits_ = bits;
        corr_range_ = 1U << bits;
    } else {
        corr_bits_ = 32;
        corr_range_ = 0;
    }

    if (corr_bits_ < 32) {
        corr_min_ = -static_cast<i32>(corr_range_ / 2);
        corr_max_ = static_cast<i32>(corr_range_ / 2 - 1);
    } else {
        corr_min_ = std::numeric_limits<i32>::min();
        corr_max_ = std::numeric_limits<i32>::max();
    }
}

IntegerCompressor::IntegerCompressor(ArithmeticDecoder* dec, u32 bits, u32 contexts,
                                     u32 bits_high, u32 range)
    : dec_(dec), bits_(bits), contexts_(contexts), bits_high_(std::min(bits_high, 11U)), range_(range) {
    if (range) {
        corr_bits_ = 0;
        corr_range_ = range;
        while (range) { range >>= 1; ++corr_bits_; }
        if (corr_range_ == (1U << (corr_bits_ - 1))) {
            --corr_bits_;
        }
    } else if (bits && bits < 32) {
        corr_bits_ = bits;
        corr_range_ = 1U << bits;
    } else {
        corr_bits_ = 32;
        corr_range_ = 0;
    }

    if (corr_bits_ < 32) {
        corr_min_ = -static_cast<i32>(corr_range_ / 2);
        corr_max_ = static_cast<i32>(corr_range_ / 2 - 1);
    } else {
        corr_min_ = std::numeric_limits<i32>::min();
        corr_max_ = std::numeric_limits<i32>::max();
    }
}

void IntegerCompressor::initCompressor() {
    m_bits_.resize(contexts_);
    for (u32 i = 0; i < contexts_; ++i) {
        m_bits_[i].init(corr_bits_ + 1, true);
    }

    m_corrector_0_.init();

    u32 num_corrector_models = corr_bits_ + 1;
    u32 max_syms = 1U << bits_high_;
    m_corrector_.resize(num_corrector_models);
    for (u32 k = 1; k <= corr_bits_; ++k) {
        u32 k_syms = (k < 32) ? (1U << k) : max_syms;
        u32 syms = std::min(k_syms, max_syms);
        m_corrector_[k].init(syms, true);
    }
}

void IntegerCompressor::initDecompressor() {
    m_bits_.resize(contexts_);
    for (u32 i = 0; i < contexts_; ++i) {
        m_bits_[i].init(corr_bits_ + 1, false);
    }

    m_corrector_0_.init();

    u32 num_corrector_models = corr_bits_ + 1;
    u32 max_syms = 1U << bits_high_;
    m_corrector_.resize(num_corrector_models);
    for (u32 k = 1; k <= corr_bits_; ++k) {
        u32 k_syms = (k < 32) ? (1U << k) : max_syms;
        u32 syms = std::min(k_syms, max_syms);
        m_corrector_[k].init(syms, false);
    }
}

void IntegerCompressor::compress(i32 pred, i32 real, u32 context) {
    i32 corr = real - pred;

    // Fold corrector into [corr_min..corr_max]
    if (corr < corr_min_) corr += static_cast<i32>(corr_range_);
    else if (corr > corr_max_) corr -= static_cast<i32>(corr_range_);

    writeCorrector(corr, m_bits_[context]);
}

i32 IntegerCompressor::decompress(i32 pred, u32 context) {
    i32 real = pred + readCorrector(m_bits_[context]);

    // Fold result into valid range
    if (real < 0) real += static_cast<i32>(corr_range_);
    else if (corr_range_ && static_cast<u32>(real) >= corr_range_) real -= static_cast<i32>(corr_range_);

    return real;
}

void IntegerCompressor::writeCorrector(i32 c, ArithmeticModel& model) {
    u32 c1;

    k_ = 0;

    // find the tightest interval [-(2^k - 1) ... +(2^k)] that contains c
    // by checking the absolute value (adjusted for the case that c is 2^k)
    c1 = (c <= 0 ? static_cast<u32>(-c) : static_cast<u32>(c - 1));

    while (c1) {
        c1 >>= 1;
        k_++;
    }

    enc_->encodeSymbol(model, k_);

    if (k_) {
        if (k_ < 32) {
            if (c < 0) {
                c += ((1 << k_) - 1);
            } else {
                c -= 1;
            }
            if (k_ <= bits_high_) {
                enc_->encodeSymbol(m_corrector_[k_], static_cast<u32>(c));
            } else {
                u32 k1 = k_ - bits_high_;
                c1 = static_cast<u32>(c) & ((1U << k1) - 1);
                c >>= k1;
                enc_->encodeSymbol(m_corrector_[k_], static_cast<u32>(c));
                enc_->writeBits(k1, c1);
            }
        }
    } else {
        enc_->encodeBit(m_corrector_0_, static_cast<u32>(c));
    }
}

i32 IntegerCompressor::readCorrector(ArithmeticModel& model) {
    i32 c;

    k_ = dec_->decodeSymbol(model);

    if (k_) {
        if (k_ < 32) {
            if (k_ <= bits_high_) {
                c = static_cast<i32>(dec_->decodeSymbol(m_corrector_[k_]));
            } else {
                u32 k1 = k_ - bits_high_;
                c = static_cast<i32>(dec_->decodeSymbol(m_corrector_[k_]));
                i32 c1 = static_cast<i32>(dec_->readBits(k1));
                c = (c << k1) | c1;
            }
            if (c >= (1 << (k_ - 1))) {
                c += 1;
            } else {
                c -= ((1 << k_) - 1);
            }
        } else {
            c = corr_min_;
        }
    } else {
        c = static_cast<i32>(dec_->decodeBit(m_corrector_0_));
    }

    return c;
}

} // namespace picolaz
