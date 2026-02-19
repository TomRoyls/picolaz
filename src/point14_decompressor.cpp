#include "picolaz/point14_decompressor.hpp"
#include <algorithm>
#include <cstring>

namespace picolaz {

Point14Decompressor::Point14Decompressor() = default;

void Point14Decompressor::chunk_sizes(ByteStreamIn* in) {
    for (u32 i = 0; i < POINT14_NUM_LAYERS; ++i) {
        in->get_bytes(reinterpret_cast<u8*>(&layer_sizes_[i]), 4);
    }
}

void Point14Decompressor::init(ByteStreamIn* in) {
    first_point_ = true;
    current_context_ = 0;

    u32 total = 0;
    for (u32 i = 0; i < POINT14_NUM_LAYERS; ++i) total += layer_sizes_[i];

    layer_data_.resize(total);
    in->get_bytes(layer_data_.data(), total);

    u32 offset = 0;
    for (u32 i = 0; i < POINT14_NUM_LAYERS; ++i) {
        layer_in_[i].init(layer_data_.data() + offset, layer_sizes_[i]);
        offset += layer_sizes_[i];
    }

    for (auto& ctx : contexts_) {
        ctx.unused = true;
    }
}

void Point14Decompressor::init_context(u32 sc) {
    auto& ctx = contexts_[sc];
    ctx.unused = false;

    auto& dec_crxy = layer_dec_[LAYER_CHANNEL_RETURNS_XY];
    auto& dec_z = layer_dec_[LAYER_Z];
    auto& dec_cls = layer_dec_[LAYER_CLASSIFICATION];
    auto& dec_flags = layer_dec_[LAYER_FLAGS];
    auto& dec_int = layer_dec_[LAYER_INTENSITY];
    auto& dec_sa = layer_dec_[LAYER_SCAN_ANGLE];
    auto& dec_ud = layer_dec_[LAYER_USER_DATA];
    auto& dec_ps = layer_dec_[LAYER_POINT_SOURCE];
    auto& dec_gps = layer_dec_[LAYER_GPSTIME];

    ctx.m_changed_values = std::make_unique<ArithmeticModel[]>(8);
    for (u32 i = 0; i < 8; ++i) ctx.m_changed_values[i].init(128, false);

    ctx.m_scanner_channel = std::make_unique<ArithmeticModel>();
    ctx.m_scanner_channel->init(3, false);

    ctx.m_number_of_returns = std::make_unique<ArithmeticModel[]>(16);
    for (u32 i = 0; i < 16; ++i) ctx.m_number_of_returns[i].init(16, false);

    ctx.m_return_number = std::make_unique<ArithmeticModel[]>(16);
    for (u32 i = 0; i < 16; ++i) ctx.m_return_number[i].init(16, false);

    ctx.m_return_number_gps_same = std::make_unique<ArithmeticModel>();
    ctx.m_return_number_gps_same->init(13, false);

    ctx.ic_dX = std::make_unique<IntegerCompressor>(&dec_crxy, 32, 2);
    ctx.ic_dX->initDecompressor();
    ctx.ic_dY = std::make_unique<IntegerCompressor>(&dec_crxy, 32, 22);
    ctx.ic_dY->initDecompressor();
    ctx.ic_Z = std::make_unique<IntegerCompressor>(&dec_z, 32, 20);
    ctx.ic_Z->initDecompressor();
    ctx.ic_intensity = std::make_unique<IntegerCompressor>(&dec_int, 16, 4);
    ctx.ic_intensity->initDecompressor();
    ctx.ic_scan_angle = std::make_unique<IntegerCompressor>(&dec_sa, 16, 2);
    ctx.ic_scan_angle->initDecompressor();
    ctx.ic_point_source_ID = std::make_unique<IntegerCompressor>(&dec_ps, 16, 1);
    ctx.ic_point_source_ID->initDecompressor();
    ctx.ic_gpstime = std::make_unique<IntegerCompressor>(&dec_gps, 32, 9, 32);
    ctx.ic_gpstime->initDecompressor();

    ctx.m_classification = std::make_unique<ArithmeticModel[]>(64);
    for (u32 i = 0; i < 64; ++i) ctx.m_classification[i].init(256, false);

    ctx.m_flags = std::make_unique<ArithmeticModel[]>(64);
    for (u32 i = 0; i < 64; ++i) ctx.m_flags[i].init(64, false);

    ctx.m_user_data = std::make_unique<ArithmeticModel[]>(64);
    for (u32 i = 0; i < 64; ++i) ctx.m_user_data[i].init(256, false);

    ctx.m_gpstime_multi = std::make_unique<ArithmeticModel>();
    ctx.m_gpstime_multi->init(GPSTIME_MULTI_TOTAL, false);

    ctx.m_gpstime_0diff = std::make_unique<ArithmeticModel>();
    ctx.m_gpstime_0diff->init(5, false);

    ctx.last_gpstime_seq = 0;
    ctx.next_gpstime_seq = 0;
    std::memset(ctx.last_gpstime, 0, sizeof(ctx.last_gpstime));
    std::memset(ctx.last_gpstime_diff, 0, sizeof(ctx.last_gpstime_diff));
    std::memset(ctx.multi_extreme_counter, 0, sizeof(ctx.multi_extreme_counter));

    for (auto& m : ctx.last_X_diff_median5) m.init();
    for (auto& m : ctx.last_Y_diff_median5) m.init();
    std::memset(ctx.last_intensity, 0, sizeof(ctx.last_intensity));
    std::memset(ctx.last_Z, 0, sizeof(ctx.last_Z));
}

void Point14Decompressor::read(u8* raw_bytes) {
    if (first_point_) {
        first_point_ = false;

        layer_in_[0].get_bytes(raw_bytes, sizeof(PackedPoint14));

        for (u32 i = 1; i < POINT14_NUM_LAYERS; ++i) {
            layer_in_[i].skip_bytes(sizeof(PackedPoint14));
        }

        for (u32 i = 0; i < POINT14_NUM_LAYERS; ++i) {
            layer_dec_[i].init(&layer_in_[i]);
        }

        auto* point = reinterpret_cast<PackedPoint14*>(raw_bytes);
        current_context_ = point->scanner_channel;
        init_context(current_context_);

        auto& ctx = contexts_[current_context_];
        std::memcpy(ctx.last_item, raw_bytes, sizeof(PackedPoint14));
        reinterpret_cast<PackedPoint14*>(ctx.last_item)->gps_time_change = false;

        auto* last = reinterpret_cast<PackedPoint14*>(ctx.last_item);
        ctx.last_gpstime[0].f = last->gps_time;
        for (u32 i = 0; i < 8; ++i) ctx.last_intensity[i] = last->intensity;
        for (u32 i = 0; i < 8; ++i) ctx.last_Z[i] = last->Z;
        return;
    }

    auto& dec_crxy = layer_dec_[LAYER_CHANNEL_RETURNS_XY];
    auto& dec_z = layer_dec_[LAYER_Z];
    auto& dec_cls = layer_dec_[LAYER_CLASSIFICATION];
    auto& dec_flags = layer_dec_[LAYER_FLAGS];
    auto& dec_int = layer_dec_[LAYER_INTENSITY];
    auto& dec_sa = layer_dec_[LAYER_SCAN_ANGLE];
    auto& dec_ud = layer_dec_[LAYER_USER_DATA];
    auto& dec_ps = layer_dec_[LAYER_POINT_SOURCE];

    auto& ctx = contexts_[current_context_];
    auto* last = reinterpret_cast<PackedPoint14*>(ctx.last_item);

    i32 lpr = (last->return_number == 1 ? 1 : 0);
    lpr += (last->return_number >= last->number_of_returns ? 2 : 0);
    lpr += (last->gps_time_change ? 4 : 0);

    u32 changed_values = dec_crxy.decodeSymbol(ctx.m_changed_values[lpr]);

    u32 scanner_channel = current_context_;
    if (changed_values & (1 << 6)) {
        u32 sym = dec_crxy.decodeSymbol(*ctx.m_scanner_channel);
        scanner_channel = (current_context_ + sym + 1) & 3;

        if (contexts_[scanner_channel].unused) {
            init_context(scanner_channel);
            auto& new_ctx = contexts_[scanner_channel];
            std::memcpy(new_ctx.last_item, ctx.last_item, sizeof(PackedPoint14));
            auto* new_last = reinterpret_cast<PackedPoint14*>(new_ctx.last_item);
            new_ctx.last_gpstime[0].f = new_last->gps_time;
            for (u32 i = 0; i < 8; ++i) new_ctx.last_intensity[i] = new_last->intensity;
            for (u32 i = 0; i < 8; ++i) new_ctx.last_Z[i] = new_last->Z;
            reinterpret_cast<PackedPoint14*>(new_ctx.last_item)->gps_time_change = false;
        }

        current_context_ = scanner_channel;
    }

    auto& cur_ctx = contexts_[scanner_channel];
    last = reinterpret_cast<PackedPoint14*>(cur_ctx.last_item);

    auto* point = reinterpret_cast<PackedPoint14*>(raw_bytes);
    std::memcpy(raw_bytes, cur_ctx.last_item, sizeof(PackedPoint14));
    point->scanner_channel = static_cast<u8>(scanner_channel);

    u32 last_n = last->number_of_returns;
    u32 last_r = last->return_number;

    if (changed_values & (1 << 2)) {
        point->number_of_returns = static_cast<u8>(
            dec_crxy.decodeSymbol(cur_ctx.m_number_of_returns[last_n]));
    }

    u32 rn_bits = changed_values & 3;
    if (rn_bits == 0) {
        // same
    } else if (rn_bits == 1) {
        point->return_number = (last_r + 1) % 16;
    } else if (rn_bits == 2) {
        point->return_number = (last_r + 15) % 16;
    } else {
        bool gps_time_change_flag = (changed_values & (1 << 4)) != 0;
        if (gps_time_change_flag) {
            point->return_number = static_cast<u8>(
                dec_crxy.decodeSymbol(cur_ctx.m_return_number[last_r]));
        } else {
            u32 sym = dec_crxy.decodeSymbol(*cur_ctx.m_return_number_gps_same);
            if (sym < 14) {
                point->return_number = static_cast<u8>((last_r + sym + 2) % 16);
            } else {
                point->return_number = static_cast<u8>((last_r + sym + 2 - 16) % 16);
            }
        }
    }

    u32 n = point->number_of_returns;
    u32 r = point->return_number;

    u32 m = number_return_map_6ctx[n < 16 ? n : 15][r < 16 ? r : 15];
    u32 l = number_return_level_8ctx[n < 16 ? n : 15][r < 16 ? r : 15];

    i32 cpr = (r == 1 ? 2 : 0);
    cpr += (r >= n ? 1 : 0);

    bool gps_time_change = (changed_values & (1 << 4)) != 0;

    u32 median_idx = (m << 1) | (gps_time_change ? 1 : 0);
    i32 median_x = cur_ctx.last_X_diff_median5[median_idx].get();
    i32 diff_x = cur_ctx.ic_dX->decompress(median_x, n == 1 ? 1 : 0);
    point->X = last->X + diff_x;
    cur_ctx.last_X_diff_median5[median_idx].add(diff_x);

    u32 k_bits_x = cur_ctx.ic_dX->getK();
    i32 median_y = cur_ctx.last_Y_diff_median5[median_idx].get();
    u32 y_ctx = (n == 1 ? 1 : 0) + (k_bits_x < 20 ? u32_zero_bit_0(k_bits_x) : 20);
    i32 diff_y = cur_ctx.ic_dY->decompress(median_y, y_ctx);
    point->Y = last->Y + diff_y;
    cur_ctx.last_Y_diff_median5[median_idx].add(diff_y);

    u32 k_bits_y = cur_ctx.ic_dY->getK();
    u32 k_bits = (k_bits_x + k_bits_y) / 2;
    u32 z_ctx = (n == 1 ? 1 : 0) + (k_bits < 18 ? u32_zero_bit_0(k_bits) : 18);
    point->Z = cur_ctx.ic_Z->decompress(cur_ctx.last_Z[l], z_ctx);
    cur_ctx.last_Z[l] = point->Z;

    u32 cls_ctx = ((last->classification & 0x1F) << 1) + (cpr == 3 ? 1 : 0);
    if (cls_ctx >= 64) cls_ctx = 63;
    point->classification = static_cast<u8>(dec_cls.decodeSymbol(cur_ctx.m_classification[cls_ctx]));

    u32 last_flags = (static_cast<u32>(last->edge_of_flight_line) << 5) |
                     (static_cast<u32>(last->scan_direction_flag) << 4) |
                     static_cast<u32>(last->classification_flags);
    u32 flags_val = dec_flags.decodeSymbol(cur_ctx.m_flags[last_flags]);
    point->edge_of_flight_line = (flags_val >> 5) & 1;
    point->scan_direction_flag = (flags_val >> 4) & 1;
    point->classification_flags = flags_val & 0x0F;

    u32 int_idx = (cpr << 1) | (gps_time_change ? 1 : 0);
    point->intensity = static_cast<u16>(
        cur_ctx.ic_intensity->decompress(cur_ctx.last_intensity[int_idx], cpr));
    cur_ctx.last_intensity[int_idx] = point->intensity;

    if (changed_values & (1 << 3)) {
        point->scan_angle = static_cast<i16>(
            cur_ctx.ic_scan_angle->decompress(last->scan_angle, gps_time_change ? 1 : 0));
    }

    u32 ud_ctx = last->user_data / 4;
    if (ud_ctx >= 64) ud_ctx = 63;
    point->user_data = static_cast<u8>(dec_ud.decodeSymbol(cur_ctx.m_user_data[ud_ctx]));

    if (changed_values & (1 << 5)) {
        point->point_source_ID = static_cast<u16>(
            cur_ctx.ic_point_source_ID->decompress(last->point_source_ID, 0));
    }

    point->gps_time_change = gps_time_change;
    if (gps_time_change) {
        read_gpstime(point, cur_ctx);
    }

    std::memcpy(cur_ctx.last_item, raw_bytes, sizeof(PackedPoint14));
}

void Point14Decompressor::read_gpstime(PackedPoint14* point, LAScontextPOINT14& ctx) {
    auto& dec = layer_dec_[LAYER_GPSTIME];

    u32 last_seq = ctx.last_gpstime_seq;

    if (ctx.last_gpstime_diff[last_seq] == 0) {
        u32 sym = dec.decodeSymbol(*ctx.m_gpstime_0diff);

        if (sym == 0) {
            i32 diff = ctx.ic_gpstime->decompress(0, 0);
            ctx.last_gpstime_diff[last_seq] = diff;
            ctx.last_gpstime[last_seq].i += diff;
            ctx.multi_extreme_counter[last_seq] = 0;
        } else if (sym == 1) {
            i32 upper = ctx.ic_gpstime->decompress(
                static_cast<i32>(ctx.last_gpstime[last_seq].u >> 32), 8);
            u32 lower = dec.readInt();
            ctx.next_gpstime_seq = (ctx.next_gpstime_seq + 1) & 3;
            ctx.last_gpstime_seq = ctx.next_gpstime_seq;
            ctx.last_gpstime[ctx.last_gpstime_seq].u =
                (static_cast<u64>(static_cast<u32>(upper)) << 32) | lower;
            ctx.last_gpstime_diff[ctx.last_gpstime_seq] = 0;
            ctx.multi_extreme_counter[ctx.last_gpstime_seq] = 0;
        } else {
            u32 seq = (last_seq + sym - 1) & 3;
            ctx.last_gpstime_seq = seq;
            read_gpstime(point, ctx);
            return;
        }
        point->gps_time = ctx.last_gpstime[ctx.last_gpstime_seq].f;
        return;
    }

    u32 sym = dec.decodeSymbol(*ctx.m_gpstime_multi);

    if (sym == 1) {
        i32 gpstime_diff = ctx.ic_gpstime->decompress(ctx.last_gpstime_diff[last_seq], 1);
        ctx.last_gpstime[last_seq].i += gpstime_diff;
        ctx.last_gpstime_diff[last_seq] = gpstime_diff;
        ctx.multi_extreme_counter[last_seq] = 0;
    } else if (sym > 1 && sym < static_cast<u32>(GPSTIME_MULTI)) {
        i32 multi = static_cast<i32>(sym);
        u32 gps_ctx = (multi < 10) ? 2 : 3;
        i32 gpstime_diff = ctx.ic_gpstime->decompress(
            multi * ctx.last_gpstime_diff[last_seq], gps_ctx);
        ctx.last_gpstime[last_seq].i += gpstime_diff;
        ctx.last_gpstime_diff[last_seq] = gpstime_diff;
    } else if (sym == static_cast<u32>(GPSTIME_MULTI)) {
        i32 gpstime_diff = ctx.ic_gpstime->decompress(
            GPSTIME_MULTI * ctx.last_gpstime_diff[last_seq], 4);
        ctx.last_gpstime[last_seq].i += gpstime_diff;
        ++ctx.multi_extreme_counter[last_seq];
        if (ctx.multi_extreme_counter[last_seq] > 3) {
            ctx.last_gpstime_diff[last_seq] = gpstime_diff;
            ctx.multi_extreme_counter[last_seq] = 0;
        }
    } else if (sym == 0) {
        i32 gpstime_diff = ctx.ic_gpstime->decompress(0, 7);
        ctx.last_gpstime[last_seq].i += gpstime_diff;
        ctx.multi_extreme_counter[last_seq]++;
        if (ctx.multi_extreme_counter[last_seq] > 3) {
            ctx.last_gpstime_diff[last_seq] = gpstime_diff;
            ctx.multi_extreme_counter[last_seq] = 0;
        }
    } else if (sym < static_cast<u32>(GPSTIME_MULTI - GPSTIME_MULTI_MINUS)) {
        i32 multi = static_cast<i32>(GPSTIME_MULTI) - static_cast<i32>(sym);
        i32 gpstime_diff = ctx.ic_gpstime->decompress(
            multi * ctx.last_gpstime_diff[last_seq], 5);
        ctx.last_gpstime[last_seq].i += gpstime_diff;
    } else if (sym == static_cast<u32>(GPSTIME_MULTI - GPSTIME_MULTI_MINUS)) {
        i32 gpstime_diff = ctx.ic_gpstime->decompress(
            GPSTIME_MULTI_MINUS * ctx.last_gpstime_diff[last_seq], 6);
        ctx.last_gpstime[last_seq].i += gpstime_diff;
        ++ctx.multi_extreme_counter[last_seq];
        if (ctx.multi_extreme_counter[last_seq] > 3) {
            ctx.last_gpstime_diff[last_seq] = gpstime_diff;
            ctx.multi_extreme_counter[last_seq] = 0;
        }
    } else {
        i32 upper = ctx.ic_gpstime->decompress(
            static_cast<i32>(ctx.last_gpstime[last_seq].u >> 32), 8);
        u32 lower = dec.readInt();
        ctx.next_gpstime_seq = (ctx.next_gpstime_seq + 1) & 3;
        ctx.last_gpstime_seq = ctx.next_gpstime_seq;
        ctx.last_gpstime[ctx.last_gpstime_seq].u =
            (static_cast<u64>(static_cast<u32>(upper)) << 32) | lower;
        ctx.last_gpstime_diff[ctx.last_gpstime_seq] = 0;
        ctx.multi_extreme_counter[ctx.last_gpstime_seq] = 0;
    }

    point->gps_time = ctx.last_gpstime[ctx.last_gpstime_seq].f;
}

} // namespace picolaz
