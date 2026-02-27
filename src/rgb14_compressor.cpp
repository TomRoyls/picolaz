// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "picolaz/rgb14_compressor.hpp"
#include <cstring>
namespace picolaz {

RGB14Compressor::RGB14Compressor() = default;

void RGB14Compressor::init(ByteStreamOut* main_out) {
    main_out_ = main_out;
    first_point_ = true;
    current_context_ = 0;

    layer_out_.clear();
    layer_enc_.init(&layer_out_);

    for (auto& ctx : contexts_) ctx.unused = true;
}

void RGB14Compressor::init_context(std::uint32_t sc) {
    auto& ctx = contexts_[sc];
    ctx.unused = false;

    ctx.m_byte_used = std::make_unique<ArithmeticModel>();
    ctx.m_byte_used->init(128, true);

    ctx.m_rgb_diff_0 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_0->init(256, true);
    ctx.m_rgb_diff_1 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_1->init(256, true);
    ctx.m_rgb_diff_2 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_2->init(256, true);
    ctx.m_rgb_diff_3 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_3->init(256, true);
    ctx.m_rgb_diff_4 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_4->init(256, true);
    ctx.m_rgb_diff_5 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_5->init(256, true);
}

void RGB14Compressor::write(const std::uint16_t* rgb) {
    if (first_point_) {
        first_point_ = false;

        layer_out_.put_bytes(reinterpret_cast<const std::uint8_t*>(rgb), 6);

        init_context(current_context_);
        auto& ctx = contexts_[current_context_];
        ctx.last_item[0] = rgb[0];
        ctx.last_item[1] = rgb[1];
        ctx.last_item[2] = rgb[2];
        return;
    }

    auto& ctx = contexts_[current_context_];

    std::int32_t diff_r = static_cast<std::int32_t>(rgb[0]) - static_cast<std::int32_t>(ctx.last_item[0]);
    std::int32_t diff_g = static_cast<std::int32_t>(rgb[1]) - static_cast<std::int32_t>(ctx.last_item[1]);
    std::int32_t diff_b = static_cast<std::int32_t>(rgb[2]) - static_cast<std::int32_t>(ctx.last_item[2]);

    auto corr_r = static_cast<std::uint16_t>(diff_r);
    auto corr_g = static_cast<std::uint16_t>(diff_g);
    auto corr_b = static_cast<std::uint16_t>(diff_b);

    std::uint8_t corr_r_lo = static_cast<std::uint8_t>(corr_r & 0xFF);
    std::uint8_t corr_r_hi = static_cast<std::uint8_t>(corr_r >> 8);
    std::uint8_t corr_g_lo = static_cast<std::uint8_t>(corr_g & 0xFF);
    std::uint8_t corr_g_hi = static_cast<std::uint8_t>(corr_g >> 8);
    std::uint8_t corr_b_lo = static_cast<std::uint8_t>(corr_b & 0xFF);
    std::uint8_t corr_b_hi = static_cast<std::uint8_t>(corr_b >> 8);

    // Build byte_used mask (7 bits)
    std::uint32_t sym = 0;
    if (corr_r_lo) sym |= 1;
    if (corr_r_hi) sym |= 2;
    if (corr_g_lo) sym |= 4;
    if (corr_g_hi) sym |= 8;
    if (corr_b_lo) sym |= 16;
    if (corr_b_hi) sym |= 32;
    // bit 6: low bytes of R and G differ
    if ((corr_r_lo != corr_g_lo) && (sym & 4)) sym |= 64;

    layer_enc_.encodeSymbol(*ctx.m_byte_used, sym);

    if (sym & 1) layer_enc_.encodeSymbol(*ctx.m_rgb_diff_0, corr_r_lo);
    if (sym & 2) layer_enc_.encodeSymbol(*ctx.m_rgb_diff_1, corr_r_hi);
    if (sym & 64) {
        layer_enc_.encodeSymbol(*ctx.m_rgb_diff_2, corr_g_lo);
    }
    if (sym & 8) layer_enc_.encodeSymbol(*ctx.m_rgb_diff_3, corr_g_hi);
    if (sym & 16) {
        std::uint8_t b_lo = (sym & 64) ? corr_b_lo : static_cast<std::uint8_t>((corr_b_lo - corr_g_lo) & 0xFF);
        layer_enc_.encodeSymbol(*ctx.m_rgb_diff_4, b_lo);
    }
    if (sym & 32) layer_enc_.encodeSymbol(*ctx.m_rgb_diff_5, corr_b_hi);

    ctx.last_item[0] = rgb[0];
    ctx.last_item[1] = rgb[1];
    ctx.last_item[2] = rgb[2];
}

void RGB14Compressor::chunk_sizes() {
    layer_enc_.done();
    std::uint32_t size = layer_out_.size();
    main_out_->put_bytes(reinterpret_cast<const std::uint8_t*>(&size), 4);
}

void RGB14Compressor::chunk_bytes() {
    if (layer_out_.size() > 0) {
        main_out_->put_bytes(layer_out_.data(), layer_out_.size());
    }
}

} // namespace picolaz
