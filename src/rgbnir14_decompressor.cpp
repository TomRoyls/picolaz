#include "picolaz/rgbnir14_decompressor.hpp"
#include <cstring>

namespace picolaz {

RGBNIR14Decompressor::RGBNIR14Decompressor() = default;

void RGBNIR14Decompressor::chunk_sizes(ByteStreamIn* in) {
    in->get_bytes(reinterpret_cast<std::uint8_t*>(&rgb_size_), 4);
    in->get_bytes(reinterpret_cast<std::uint8_t*>(&nir_size_), 4);
}

void RGBNIR14Decompressor::init(ByteStreamIn* in) {
    first_point_ = true;
    current_context_ = 0;

    rgb_data_.resize(rgb_size_);
    in->get_bytes(rgb_data_.data(), rgb_size_);
    rgb_in_.init(rgb_data_.data(), rgb_size_);

    nir_data_.resize(nir_size_);
    in->get_bytes(nir_data_.data(), nir_size_);
    nir_in_.init(nir_data_.data(), nir_size_);

    for (auto& ctx : contexts_) ctx.unused = true;
}

void RGBNIR14Decompressor::init_context(std::uint32_t sc) {
    auto& ctx = contexts_[sc];
    ctx.unused = false;

    ctx.m_byte_used = std::make_unique<ArithmeticModel>();
    ctx.m_byte_used->init(128, false);

    ctx.m_rgb_diff_0 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_0->init(256, false);
    ctx.m_rgb_diff_1 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_1->init(256, false);
    ctx.m_rgb_diff_2 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_2->init(256, false);
    ctx.m_rgb_diff_3 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_3->init(256, false);
    ctx.m_rgb_diff_4 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_4->init(256, false);
    ctx.m_rgb_diff_5 = std::make_unique<ArithmeticModel>();
    ctx.m_rgb_diff_5->init(256, false);

    ctx.m_nir_bytes_used = std::make_unique<ArithmeticModel>();
    ctx.m_nir_bytes_used->init(4, false);
    ctx.m_nir_diff_0 = std::make_unique<ArithmeticModel>();
    ctx.m_nir_diff_0->init(256, false);
    ctx.m_nir_diff_1 = std::make_unique<ArithmeticModel>();
    ctx.m_nir_diff_1->init(256, false);
}

void RGBNIR14Decompressor::read(std::uint16_t* rgbnir) {
    if (first_point_) {
        first_point_ = false;

        rgb_in_.get_bytes(reinterpret_cast<std::uint8_t*>(rgbnir), 6);
        nir_in_.get_bytes(reinterpret_cast<std::uint8_t*>(&rgbnir[3]), 2);

        rgb_dec_.init(&rgb_in_);
        nir_dec_.init(&nir_in_);

        init_context(current_context_);
        auto& ctx = contexts_[current_context_];
        ctx.last_item[0] = rgbnir[0];
        ctx.last_item[1] = rgbnir[1];
        ctx.last_item[2] = rgbnir[2];
        ctx.last_item[3] = rgbnir[3];
        return;
    }

    auto& ctx = contexts_[current_context_];

    // RGB
    std::uint32_t sym = rgb_dec_.decodeSymbol(*ctx.m_byte_used);

    std::uint8_t corr_r_lo = 0, corr_r_hi = 0;
    std::uint8_t corr_g_lo = 0, corr_g_hi = 0;
    std::uint8_t corr_b_lo = 0, corr_b_hi = 0;

    if (sym & 1) corr_r_lo = static_cast<std::uint8_t>(rgb_dec_.decodeSymbol(*ctx.m_rgb_diff_0));
    if (sym & 2) corr_r_hi = static_cast<std::uint8_t>(rgb_dec_.decodeSymbol(*ctx.m_rgb_diff_1));
    if (sym & 64) {
        corr_g_lo = static_cast<std::uint8_t>(rgb_dec_.decodeSymbol(*ctx.m_rgb_diff_2));
    } else {
        corr_g_lo = corr_r_lo;
    }
    if (sym & 8) corr_g_hi = static_cast<std::uint8_t>(rgb_dec_.decodeSymbol(*ctx.m_rgb_diff_3));
    if (sym & 16) {
        std::uint8_t b_lo_raw = static_cast<std::uint8_t>(rgb_dec_.decodeSymbol(*ctx.m_rgb_diff_4));
        corr_b_lo = (sym & 64) ? b_lo_raw : static_cast<std::uint8_t>((b_lo_raw + corr_g_lo) & 0xFF);
    }
    if (sym & 32) corr_b_hi = static_cast<std::uint8_t>(rgb_dec_.decodeSymbol(*ctx.m_rgb_diff_5));

    std::uint16_t corr_r = static_cast<std::uint16_t>(corr_r_lo) | (static_cast<std::uint16_t>(corr_r_hi) << 8);
    std::uint16_t corr_g = static_cast<std::uint16_t>(corr_g_lo) | (static_cast<std::uint16_t>(corr_g_hi) << 8);
    std::uint16_t corr_b = static_cast<std::uint16_t>(corr_b_lo) | (static_cast<std::uint16_t>(corr_b_hi) << 8);

    rgbnir[0] = ctx.last_item[0] + corr_r;
    rgbnir[1] = ctx.last_item[1] + corr_g;
    rgbnir[2] = ctx.last_item[2] + corr_b;

    // NIR
    std::uint32_t nir_sym = nir_dec_.decodeSymbol(*ctx.m_nir_bytes_used);
    std::uint8_t corr_nir_lo = 0, corr_nir_hi = 0;
    if (nir_sym & 1) corr_nir_lo = static_cast<std::uint8_t>(nir_dec_.decodeSymbol(*ctx.m_nir_diff_0));
    if (nir_sym & 2) corr_nir_hi = static_cast<std::uint8_t>(nir_dec_.decodeSymbol(*ctx.m_nir_diff_1));

    std::uint16_t corr_nir = static_cast<std::uint16_t>(corr_nir_lo) | (static_cast<std::uint16_t>(corr_nir_hi) << 8);
    rgbnir[3] = ctx.last_item[3] + corr_nir;

    ctx.last_item[0] = rgbnir[0];
    ctx.last_item[1] = rgbnir[1];
    ctx.last_item[2] = rgbnir[2];
    ctx.last_item[3] = rgbnir[3];
}

} // namespace picolaz
