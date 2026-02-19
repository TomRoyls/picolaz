#include "picolaz/rgbnir14_compressor.hpp"
#include <cstring>

namespace picolaz {

RGBNIR14Compressor::RGBNIR14Compressor() = default;

void RGBNIR14Compressor::init(ByteStreamOut* main_out) {
    main_out_ = main_out;
    first_point_ = true;
    current_context_ = 0;

    rgb_out_.clear();
    nir_out_.clear();
    rgb_enc_.init(&rgb_out_);
    nir_enc_.init(&nir_out_);

    for (auto& ctx : contexts_) ctx.unused = true;
}

void RGBNIR14Compressor::init_context(u32 sc) {
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

    ctx.m_nir_bytes_used = std::make_unique<ArithmeticModel>();
    ctx.m_nir_bytes_used->init(4, true);
    ctx.m_nir_diff_0 = std::make_unique<ArithmeticModel>();
    ctx.m_nir_diff_0->init(256, true);
    ctx.m_nir_diff_1 = std::make_unique<ArithmeticModel>();
    ctx.m_nir_diff_1->init(256, true);
}

void RGBNIR14Compressor::write(const u16* rgbnir) {
    if (first_point_) {
        first_point_ = false;

        rgb_out_.put_bytes(reinterpret_cast<const u8*>(rgbnir), 6);
        nir_out_.put_bytes(reinterpret_cast<const u8*>(&rgbnir[3]), 2);

        init_context(current_context_);
        auto& ctx = contexts_[current_context_];
        ctx.last_item[0] = rgbnir[0];
        ctx.last_item[1] = rgbnir[1];
        ctx.last_item[2] = rgbnir[2];
        ctx.last_item[3] = rgbnir[3];
        return;
    }

    auto& ctx = contexts_[current_context_];

    // RGB part
    i32 diff_r = static_cast<i32>(rgbnir[0]) - static_cast<i32>(ctx.last_item[0]);
    i32 diff_g = static_cast<i32>(rgbnir[1]) - static_cast<i32>(ctx.last_item[1]);
    i32 diff_b = static_cast<i32>(rgbnir[2]) - static_cast<i32>(ctx.last_item[2]);

    auto corr_r = static_cast<u16>(diff_r);
    auto corr_g = static_cast<u16>(diff_g);
    auto corr_b = static_cast<u16>(diff_b);

    u8 corr_r_lo = static_cast<u8>(corr_r & 0xFF);
    u8 corr_r_hi = static_cast<u8>(corr_r >> 8);
    u8 corr_g_lo = static_cast<u8>(corr_g & 0xFF);
    u8 corr_g_hi = static_cast<u8>(corr_g >> 8);
    u8 corr_b_lo = static_cast<u8>(corr_b & 0xFF);
    u8 corr_b_hi = static_cast<u8>(corr_b >> 8);

    u32 sym = 0;
    if (corr_r_lo) sym |= 1;
    if (corr_r_hi) sym |= 2;
    if (corr_g_lo) sym |= 4;
    if (corr_g_hi) sym |= 8;
    if (corr_b_lo) sym |= 16;
    if (corr_b_hi) sym |= 32;
    if ((corr_r_lo != corr_g_lo) && (sym & 4)) sym |= 64;

    rgb_enc_.encodeSymbol(*ctx.m_byte_used, sym);

    if (sym & 1) rgb_enc_.encodeSymbol(*ctx.m_rgb_diff_0, corr_r_lo);
    if (sym & 2) rgb_enc_.encodeSymbol(*ctx.m_rgb_diff_1, corr_r_hi);
    if (sym & 64) rgb_enc_.encodeSymbol(*ctx.m_rgb_diff_2, corr_g_lo);
    if (sym & 8) rgb_enc_.encodeSymbol(*ctx.m_rgb_diff_3, corr_g_hi);
    if (sym & 16) {
        u8 b_lo = (sym & 64) ? corr_b_lo : static_cast<u8>((corr_b_lo - corr_g_lo) & 0xFF);
        rgb_enc_.encodeSymbol(*ctx.m_rgb_diff_4, b_lo);
    }
    if (sym & 32) rgb_enc_.encodeSymbol(*ctx.m_rgb_diff_5, corr_b_hi);

    // NIR part
    i32 diff_nir = static_cast<i32>(rgbnir[3]) - static_cast<i32>(ctx.last_item[3]);
    auto corr_nir = static_cast<u16>(diff_nir);
    u8 corr_nir_lo = static_cast<u8>(corr_nir & 0xFF);
    u8 corr_nir_hi = static_cast<u8>(corr_nir >> 8);

    u32 nir_sym = 0;
    if (corr_nir_lo) nir_sym |= 1;
    if (corr_nir_hi) nir_sym |= 2;

    nir_enc_.encodeSymbol(*ctx.m_nir_bytes_used, nir_sym);
    if (nir_sym & 1) nir_enc_.encodeSymbol(*ctx.m_nir_diff_0, corr_nir_lo);
    if (nir_sym & 2) nir_enc_.encodeSymbol(*ctx.m_nir_diff_1, corr_nir_hi);

    ctx.last_item[0] = rgbnir[0];
    ctx.last_item[1] = rgbnir[1];
    ctx.last_item[2] = rgbnir[2];
    ctx.last_item[3] = rgbnir[3];
}

void RGBNIR14Compressor::chunk_sizes() {
    rgb_enc_.done();
    nir_enc_.done();

    u32 rgb_size = rgb_out_.size();
    u32 nir_size = nir_out_.size();
    main_out_->put_bytes(reinterpret_cast<const u8*>(&rgb_size), 4);
    main_out_->put_bytes(reinterpret_cast<const u8*>(&nir_size), 4);
}

void RGBNIR14Compressor::chunk_bytes() {
    if (rgb_out_.size() > 0) main_out_->put_bytes(rgb_out_.data(), rgb_out_.size());
    if (nir_out_.size() > 0) main_out_->put_bytes(nir_out_.data(), nir_out_.size());
}

} // namespace picolaz
