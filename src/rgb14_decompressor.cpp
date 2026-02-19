#include "picolaz/rgb14_decompressor.hpp"
#include <cstring>

namespace picolaz {

RGB14Decompressor::RGB14Decompressor() = default;

void RGB14Decompressor::chunk_sizes(ByteStreamIn* in) {
    in->get_bytes(reinterpret_cast<std::uint8_t*>(&layer_size_), 4);
}

void RGB14Decompressor::init(ByteStreamIn* in) {
    first_point_ = true;
    current_context_ = 0;

    layer_data_.resize(layer_size_);
    in->get_bytes(layer_data_.data(), layer_size_);
    layer_in_.init(layer_data_.data(), layer_size_);

    for (auto& ctx : contexts_) ctx.unused = true;
}

void RGB14Decompressor::init_context(std::uint32_t sc) {
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
}

void RGB14Decompressor::read(std::uint16_t* rgb) {
    if (first_point_) {
        first_point_ = false;

        layer_in_.get_bytes(reinterpret_cast<std::uint8_t*>(rgb), 6);
        layer_dec_.init(&layer_in_);

        init_context(current_context_);
        auto& ctx = contexts_[current_context_];
        ctx.last_item[0] = rgb[0];
        ctx.last_item[1] = rgb[1];
        ctx.last_item[2] = rgb[2];
        return;
    }

    auto& ctx = contexts_[current_context_];

    std::uint32_t sym = layer_dec_.decodeSymbol(*ctx.m_byte_used);

    std::uint8_t corr_r_lo = 0, corr_r_hi = 0;
    std::uint8_t corr_g_lo = 0, corr_g_hi = 0;
    std::uint8_t corr_b_lo = 0, corr_b_hi = 0;

    if (sym & 1) corr_r_lo = static_cast<std::uint8_t>(layer_dec_.decodeSymbol(*ctx.m_rgb_diff_0));
    if (sym & 2) corr_r_hi = static_cast<std::uint8_t>(layer_dec_.decodeSymbol(*ctx.m_rgb_diff_1));
    if (sym & 64) {
        corr_g_lo = static_cast<std::uint8_t>(layer_dec_.decodeSymbol(*ctx.m_rgb_diff_2));
    } else {
        corr_g_lo = corr_r_lo;
    }
    if (sym & 8) corr_g_hi = static_cast<std::uint8_t>(layer_dec_.decodeSymbol(*ctx.m_rgb_diff_3));
    if (sym & 16) {
        std::uint8_t b_lo_raw = static_cast<std::uint8_t>(layer_dec_.decodeSymbol(*ctx.m_rgb_diff_4));
        corr_b_lo = (sym & 64) ? b_lo_raw : static_cast<std::uint8_t>((b_lo_raw + corr_g_lo) & 0xFF);
    }
    if (sym & 32) corr_b_hi = static_cast<std::uint8_t>(layer_dec_.decodeSymbol(*ctx.m_rgb_diff_5));

    std::uint16_t corr_r = static_cast<std::uint16_t>(corr_r_lo) | (static_cast<std::uint16_t>(corr_r_hi) << 8);
    std::uint16_t corr_g = static_cast<std::uint16_t>(corr_g_lo) | (static_cast<std::uint16_t>(corr_g_hi) << 8);
    std::uint16_t corr_b = static_cast<std::uint16_t>(corr_b_lo) | (static_cast<std::uint16_t>(corr_b_hi) << 8);

    rgb[0] = ctx.last_item[0] + corr_r;
    rgb[1] = ctx.last_item[1] + corr_g;
    rgb[2] = ctx.last_item[2] + corr_b;

    ctx.last_item[0] = rgb[0];
    ctx.last_item[1] = rgb[1];
    ctx.last_item[2] = rgb[2];
}

} // namespace picolaz
