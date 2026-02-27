// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "picolaz/integer_compressor.hpp"
#include <algorithm>
#include <limits>
namespace picolaz {

IntegerCompressor::IntegerCompressor(ArithmeticEncoder* enc, std::uint32_t bits, std::uint32_t contexts,
                                     std::uint32_t bits_high, std::uint32_t range)
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
        corr_min_ = -static_cast<std::int32_t>(corr_range_ / 2);
        corr_max_ = static_cast<std::int32_t>(corr_range_ / 2 - 1);
    } else {
        corr_min_ = std::numeric_limits<std::int32_t>::min();
        corr_max_ = std::numeric_limits<std::int32_t>::max();
    }
}

IntegerCompressor::IntegerCompressor(ArithmeticDecoder* dec, std::uint32_t bits, std::uint32_t contexts,
                                     std::uint32_t bits_high, std::uint32_t range)
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
        corr_min_ = -static_cast<std::int32_t>(corr_range_ / 2);
        corr_max_ = static_cast<std::int32_t>(corr_range_ / 2 - 1);
    } else {
        corr_min_ = std::numeric_limits<std::int32_t>::min();
        corr_max_ = std::numeric_limits<std::int32_t>::max();
    }
}

void IntegerCompressor::initCompressor() {
    m_bits_.resize(contexts_);
    for (std::uint32_t i = 0; i < contexts_; ++i) {
        m_bits_[i].init(corr_bits_ + 1, true);
    }

    m_corrector_0_.init();

    std::uint32_t num_corrector_models = corr_bits_ + 1;
    std::uint32_t max_syms = 1U << bits_high_;
    m_corrector_.resize(num_corrector_models);
    for (std::uint32_t k = 1; k <= corr_bits_; ++k) {
        std::uint32_t k_syms = (k < 32) ? (1U << k) : max_syms;
        std::uint32_t syms = std::min(k_syms, max_syms);
        m_corrector_[k].init(syms, true);
    }
}

void IntegerCompressor::initDecompressor() {
    m_bits_.resize(contexts_);
    for (std::uint32_t i = 0; i < contexts_; ++i) {
        m_bits_[i].init(corr_bits_ + 1, false);
    }

    m_corrector_0_.init();

    std::uint32_t num_corrector_models = corr_bits_ + 1;
    std::uint32_t max_syms = 1U << bits_high_;
    m_corrector_.resize(num_corrector_models);
    for (std::uint32_t k = 1; k <= corr_bits_; ++k) {
        std::uint32_t k_syms = (k < 32) ? (1U << k) : max_syms;
        std::uint32_t syms = std::min(k_syms, max_syms);
        m_corrector_[k].init(syms, false);
    }
}

void IntegerCompressor::compress(std::int32_t pred, std::int32_t real, std::uint32_t context) {
    std::int32_t corr = real - pred;

    // Fold corrector into [corr_min..corr_max]
    if (corr < corr_min_) corr += static_cast<std::int32_t>(corr_range_);
    else if (corr > corr_max_) corr -= static_cast<std::int32_t>(corr_range_);

    writeCorrector(corr, m_bits_[context]);
}

std::int32_t IntegerCompressor::decompress(std::int32_t pred, std::uint32_t context) {
    std::int32_t real = pred + readCorrector(m_bits_[context]);

    // Fold result into valid range
    if (real < 0) real += static_cast<std::int32_t>(corr_range_);
    else if (corr_range_ && static_cast<std::uint32_t>(real) >= corr_range_) real -= static_cast<std::int32_t>(corr_range_);

    return real;
}

void IntegerCompressor::writeCorrector(std::int32_t c, ArithmeticModel& model) {
    std::uint32_t c1;

    k_ = 0;

    // find the tightest interval [-(2^k - 1) ... +(2^k)] that contains c
    // by checking the absolute value (adjusted for the case that c is 2^k)
    c1 = (c <= 0 ? static_cast<std::uint32_t>(-c) : static_cast<std::uint32_t>(c - 1));

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
                enc_->encodeSymbol(m_corrector_[k_], static_cast<std::uint32_t>(c));
            } else {
                std::uint32_t k1 = k_ - bits_high_;
                c1 = static_cast<std::uint32_t>(c) & ((1U << k1) - 1);
                c >>= k1;
                enc_->encodeSymbol(m_corrector_[k_], static_cast<std::uint32_t>(c));
                enc_->writeBits(k1, c1);
            }
        }
    } else {
        enc_->encodeBit(m_corrector_0_, static_cast<std::uint32_t>(c));
    }
}

std::int32_t IntegerCompressor::readCorrector(ArithmeticModel& model) {
    std::int32_t c;

    k_ = dec_->decodeSymbol(model);

    if (k_) {
        if (k_ < 32) {
            if (k_ <= bits_high_) {
                c = static_cast<std::int32_t>(dec_->decodeSymbol(m_corrector_[k_]));
            } else {
                std::uint32_t k1 = k_ - bits_high_;
                c = static_cast<std::int32_t>(dec_->decodeSymbol(m_corrector_[k_]));
                std::int32_t c1 = static_cast<std::int32_t>(dec_->readBits(k1));
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
        c = static_cast<std::int32_t>(dec_->decodeBit(m_corrector_0_));
    }

    return c;
}

} // namespace picolaz
