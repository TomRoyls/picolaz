#include "picolaz/point14_compressor.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace picolaz {

Point14Compressor::Point14Compressor() = default;

void Point14Compressor::init(ByteStreamOut* main_out) {
    main_out_ = main_out;
    first_point_ = true;
    current_context_ = 0;

    for (u32 i = 0; i < POINT14_NUM_LAYERS; ++i) {
        layer_out_[i].clear();
        layer_enc_[i].init(&layer_out_[i]);
    }

    for (auto& ctx : contexts_) {
        ctx.unused = true;
    }
}

void Point14Compressor::init_context(u32 sc) {
    auto& ctx = contexts_[sc];
    ctx.unused = false;

    auto& enc_crxy = layer_enc_[LAYER_CHANNEL_RETURNS_XY];
    auto& enc_z = layer_enc_[LAYER_Z];
    auto& enc_cls = layer_enc_[LAYER_CLASSIFICATION];
    auto& enc_flags = layer_enc_[LAYER_FLAGS];
    auto& enc_int = layer_enc_[LAYER_INTENSITY];
    auto& enc_sa = layer_enc_[LAYER_SCAN_ANGLE];
    auto& enc_ud = layer_enc_[LAYER_USER_DATA];
    auto& enc_ps = layer_enc_[LAYER_POINT_SOURCE];
    auto& enc_gps = layer_enc_[LAYER_GPSTIME];

    ctx.m_changed_values = std::make_unique<ArithmeticModel[]>(8);
    for (u32 i = 0; i < 8; ++i) ctx.m_changed_values[i].init(128, true);

    ctx.m_scanner_channel = std::make_unique<ArithmeticModel>();
    ctx.m_scanner_channel->init(3, true);

    ctx.m_number_of_returns = std::make_unique<ArithmeticModel[]>(16);
    for (u32 i = 0; i < 16; ++i) ctx.m_number_of_returns[i].init(16, true);

    ctx.m_return_number = std::make_unique<ArithmeticModel[]>(16);
    for (u32 i = 0; i < 16; ++i) ctx.m_return_number[i].init(16, true);

    ctx.m_return_number_gps_same = std::make_unique<ArithmeticModel>();
    ctx.m_return_number_gps_same->init(13, true);

    ctx.ic_dX = std::make_unique<IntegerCompressor>(&enc_crxy, 32, 2);
    ctx.ic_dX->initCompressor();
    ctx.ic_dY = std::make_unique<IntegerCompressor>(&enc_crxy, 32, 22);
    ctx.ic_dY->initCompressor();
    ctx.ic_Z = std::make_unique<IntegerCompressor>(&enc_z, 32, 20);
    ctx.ic_Z->initCompressor();
    ctx.ic_intensity = std::make_unique<IntegerCompressor>(&enc_int, 16, 4);
    ctx.ic_intensity->initCompressor();
    ctx.ic_scan_angle = std::make_unique<IntegerCompressor>(&enc_sa, 16, 2);
    ctx.ic_scan_angle->initCompressor();
    ctx.ic_point_source_ID = std::make_unique<IntegerCompressor>(&enc_ps, 16, 1);
    ctx.ic_point_source_ID->initCompressor();
    ctx.ic_gpstime = std::make_unique<IntegerCompressor>(&enc_gps, 32, 9, 32);
    ctx.ic_gpstime->initCompressor();

    ctx.m_classification = std::make_unique<ArithmeticModel[]>(64);
    for (u32 i = 0; i < 64; ++i) ctx.m_classification[i].init(256, true);

    ctx.m_flags = std::make_unique<ArithmeticModel[]>(64);
    for (u32 i = 0; i < 64; ++i) ctx.m_flags[i].init(64, true);

    ctx.m_user_data = std::make_unique<ArithmeticModel[]>(64);
    for (u32 i = 0; i < 64; ++i) ctx.m_user_data[i].init(256, true);

    ctx.m_gpstime_multi = std::make_unique<ArithmeticModel>();
    ctx.m_gpstime_multi->init(GPSTIME_MULTI_TOTAL, true);

    ctx.m_gpstime_0diff = std::make_unique<ArithmeticModel>();
    ctx.m_gpstime_0diff->init(5, true);

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

void Point14Compressor::write(const u8* raw_bytes) {
    auto* point = reinterpret_cast<const PackedPoint14*>(raw_bytes);

    if (first_point_) {
        first_point_ = false;

        // Write raw first point to all layers
        for (u32 i = 0; i < POINT14_NUM_LAYERS; ++i) {
            layer_out_[i].put_bytes(raw_bytes, sizeof(PackedPoint14));
        }

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

    auto& enc_crxy = layer_enc_[LAYER_CHANNEL_RETURNS_XY];
    auto& enc_cls = layer_enc_[LAYER_CLASSIFICATION];
    auto& enc_flags = layer_enc_[LAYER_FLAGS];
    auto& enc_ud = layer_enc_[LAYER_USER_DATA];

    // Get last item from current context
    u8* last_item = contexts_[current_context_].last_item;
    auto* last = reinterpret_cast<PackedPoint14*>(last_item);

    // Compute lpr from LAST point — matches LASzip exactly
    // single(3) / first(1) / last(2) / intermediate(0)
    i32 lpr = (last->return_number == 1 ? 1 : 0);
    lpr += (last->return_number >= last->number_of_returns ? 2 : 0);
    lpr += (last->gps_time_change ? 4 : 0);

    u32 scanner_channel = point->scanner_channel;

    // If scanner channel changed and new context exists, get last from new context
    if (scanner_channel != current_context_) {
        if (!contexts_[scanner_channel].unused) {
            last_item = contexts_[scanner_channel].last_item;
            last = reinterpret_cast<PackedPoint14*>(last_item);
        }
    }

    // Determine changed attributes
    bool point_source_change = (point->point_source_ID != last->point_source_ID);
    bool gps_time_change = (point->gps_time != last->gps_time);
    bool scan_angle_change = (point->scan_angle != last->scan_angle);

    u32 last_n = last->number_of_returns;
    u32 last_r = last->return_number;
    u32 n = point->number_of_returns;
    u32 r = point->return_number;

    // Build 7-bit changed_values mask
    i32 changed_values = ((scanner_channel != current_context_) ? (1 << 6) : 0) |
                         (point_source_change ? (1 << 5) : 0) |
                         (gps_time_change ? (1 << 4) : 0) |
                         (scan_angle_change ? (1 << 3) : 0) |
                         ((n != last_n) ? (1 << 2) : 0);

    if (r != last_r) {
        if (r == ((last_r + 1) % 16)) {
            changed_values |= 1;
        } else if (r == ((last_r + 15) % 16)) {
            changed_values |= 2;
        } else {
            changed_values |= 3;
        }
    }

    enc_crxy.encodeSymbol(contexts_[current_context_].m_changed_values[lpr], changed_values);

    // Scanner channel change
    if (changed_values & (1 << 6)) {
        i32 diff = static_cast<i32>(scanner_channel) - static_cast<i32>(current_context_);
        if (diff > 0) {
            enc_crxy.encodeSymbol(*contexts_[current_context_].m_scanner_channel, diff - 1);
        } else {
            enc_crxy.encodeSymbol(*contexts_[current_context_].m_scanner_channel, diff + 4 - 1);
        }

        if (contexts_[scanner_channel].unused) {
            init_context(scanner_channel);
            auto& new_ctx = contexts_[scanner_channel];
            std::memcpy(new_ctx.last_item, contexts_[current_context_].last_item, sizeof(PackedPoint14));
            auto* new_last = reinterpret_cast<PackedPoint14*>(new_ctx.last_item);
            new_ctx.last_gpstime[0].f = new_last->gps_time;
            for (u32 i = 0; i < 8; ++i) new_ctx.last_intensity[i] = new_last->intensity;
            for (u32 i = 0; i < 8; ++i) new_ctx.last_Z[i] = new_last->Z;
            reinterpret_cast<PackedPoint14*>(new_ctx.last_item)->gps_time_change = false;
            last_item = new_ctx.last_item;
            last = reinterpret_cast<PackedPoint14*>(last_item);
        }

        current_context_ = scanner_channel;
    }

    auto& ctx = contexts_[current_context_];

    // Number of returns
    if (changed_values & (1 << 2)) {
        enc_crxy.encodeSymbol(ctx.m_number_of_returns[last_n], n);
    }

    // Return number
    if ((changed_values & 3) == 3) {
        if (gps_time_change) {
            enc_crxy.encodeSymbol(ctx.m_return_number[last_r], r);
        } else {
            i32 rdiff = static_cast<i32>(r) - static_cast<i32>(last_r);
            if (rdiff > 1) {
                enc_crxy.encodeSymbol(*ctx.m_return_number_gps_same, rdiff - 2);
            } else {
                enc_crxy.encodeSymbol(*ctx.m_return_number_gps_same, rdiff + 16 - 2);
            }
        }
    }

    // Get return map m and return level l
    u32 m = number_return_map_6ctx[n][r];
    u32 l = number_return_level_8ctx[n][r];

    // Create single(3)/first(2)/last(1)/intermediate(0) return context
    i32 cpr = (r == 1 ? 2 : 0);
    cpr += (r >= n ? 1 : 0);

    // X coordinate
    i32 median_x = ctx.last_X_diff_median5[(m << 1) | (gps_time_change ? 1 : 0)].get();
    i32 diff_x = point->X - last->X;
    ctx.ic_dX->compress(median_x, diff_x, n == 1 ? 1 : 0);
    ctx.last_X_diff_median5[(m << 1) | (gps_time_change ? 1 : 0)].add(diff_x);

    // Y coordinate
    u32 k_bits_x = ctx.ic_dX->getK();
    i32 median_y = ctx.last_Y_diff_median5[(m << 1) | (gps_time_change ? 1 : 0)].get();
    i32 diff_y = point->Y - last->Y;
    u32 y_ctx = (n == 1 ? 1 : 0) + (k_bits_x < 20 ? u32_zero_bit_0(k_bits_x) : 20);
    ctx.ic_dY->compress(median_y, diff_y, y_ctx);
    ctx.last_Y_diff_median5[(m << 1) | (gps_time_change ? 1 : 0)].add(diff_y);

    // Z coordinate
    u32 k_bits_y = ctx.ic_dY->getK();
    u32 k_bits = (k_bits_x + k_bits_y) / 2;
    u32 z_ctx = (n == 1 ? 1 : 0) + (k_bits < 18 ? u32_zero_bit_0(k_bits) : 18);
    ctx.ic_Z->compress(ctx.last_Z[l], point->Z, z_ctx);
    ctx.last_Z[l] = point->Z;

    // Classification
    u32 cls_ctx = ((last->classification & 0x1F) << 1) + (cpr == 3 ? 1 : 0);
    enc_cls.encodeSymbol(ctx.m_classification[cls_ctx], point->classification);

    // Flags
    u32 last_flags = (static_cast<u32>(last->edge_of_flight_line) << 5) |
                     (static_cast<u32>(last->scan_direction_flag) << 4) |
                     static_cast<u32>(last->classification_flags);
    u32 flags_val = (static_cast<u32>(point->edge_of_flight_line) << 5) |
                    (static_cast<u32>(point->scan_direction_flag) << 4) |
                    static_cast<u32>(point->classification_flags);
    enc_flags.encodeSymbol(ctx.m_flags[last_flags], flags_val);

    // Intensity
    ctx.ic_intensity->compress(ctx.last_intensity[(cpr << 1) | (gps_time_change ? 1 : 0)],
                               point->intensity, cpr);
    ctx.last_intensity[(cpr << 1) | (gps_time_change ? 1 : 0)] = point->intensity;

    // Scan angle
    if (scan_angle_change) {
        ctx.ic_scan_angle->compress(last->scan_angle, point->scan_angle,
                                     gps_time_change ? 1 : 0);
    }

    // User data
    u32 ud_ctx = last->user_data / 4;
    enc_ud.encodeSymbol(ctx.m_user_data[ud_ctx], point->user_data);

    // Point source ID
    if (point_source_change) {
        ctx.ic_point_source_ID->compress(last->point_source_ID, point->point_source_ID, 0);
    }

    // GPS time
    if (gps_time_change) {
        write_gpstime(point, ctx);
    }

    // Copy last item and remember gps_time_change
    std::memcpy(ctx.last_item, raw_bytes, sizeof(PackedPoint14));
    reinterpret_cast<PackedPoint14*>(ctx.last_item)->gps_time_change = gps_time_change;
}

void Point14Compressor::write_gpstime(const PackedPoint14* point, LAScontextPOINT14& ctx) {
    auto& enc = layer_enc_[LAYER_GPSTIME];

    U64I64F64 this_gpstime;
    this_gpstime.f = point->gps_time;

    u32 last_seq = ctx.last_gpstime_seq;

    if (ctx.last_gpstime_diff[last_seq] == 0) {
        i64 curr_gpstime_diff_64 = this_gpstime.i - ctx.last_gpstime[last_seq].i;
        i32 curr_gpstime_diff = static_cast<i32>(curr_gpstime_diff_64);

        if (curr_gpstime_diff_64 == static_cast<i64>(curr_gpstime_diff)) {
            // Fits in 32 bits
            enc.encodeSymbol(*ctx.m_gpstime_0diff, 0);
            ctx.ic_gpstime->compress(0, curr_gpstime_diff, 0);
            ctx.last_gpstime_diff[last_seq] = curr_gpstime_diff;
            ctx.multi_extreme_counter[last_seq] = 0;
        } else {
            // Try other sequences
            for (u32 i = 1; i < 4; ++i) {
                i64 other_diff = this_gpstime.i - ctx.last_gpstime[(last_seq + i) & 3].i;
                i32 other_diff_32 = static_cast<i32>(other_diff);
                if (other_diff == static_cast<i64>(other_diff_32)) {
                    enc.encodeSymbol(*ctx.m_gpstime_0diff, i + 1);
                    ctx.last_gpstime_seq = (last_seq + i) & 3;
                    write_gpstime(point, ctx);
                    return;
                }
            }
            // Start new sequence
            enc.encodeSymbol(*ctx.m_gpstime_0diff, 1);
            ctx.ic_gpstime->compress(static_cast<i32>(ctx.last_gpstime[last_seq].u >> 32),
                                     static_cast<i32>(this_gpstime.u >> 32), 8);
            enc.writeInt(static_cast<u32>(this_gpstime.u));
            ctx.next_gpstime_seq = (ctx.next_gpstime_seq + 1) & 3;
            ctx.last_gpstime_seq = ctx.next_gpstime_seq;
            ctx.last_gpstime_diff[ctx.last_gpstime_seq] = 0;
            ctx.multi_extreme_counter[ctx.last_gpstime_seq] = 0;
        }
        ctx.last_gpstime[ctx.last_gpstime_seq].i = this_gpstime.i;
        return;
    }

    // last_gpstime_diff != 0
    i64 curr_gpstime_diff_64 = this_gpstime.i - ctx.last_gpstime[last_seq].i;
    i32 curr_gpstime_diff = static_cast<i32>(curr_gpstime_diff_64);

    if (curr_gpstime_diff_64 == static_cast<i64>(curr_gpstime_diff)) {
        f32 multi_f = static_cast<f32>(curr_gpstime_diff) /
                      static_cast<f32>(ctx.last_gpstime_diff[last_seq]);
        i32 multi = i32_quantize(multi_f);

        if (multi == 1) {
            enc.encodeSymbol(*ctx.m_gpstime_multi, 1);
            ctx.ic_gpstime->compress(ctx.last_gpstime_diff[last_seq], curr_gpstime_diff, 1);
            ctx.multi_extreme_counter[last_seq] = 0;
        } else if (multi > 0) {
            if (multi < GPSTIME_MULTI) {
                enc.encodeSymbol(*ctx.m_gpstime_multi, multi);
                u32 gps_ctx = (multi < 10) ? 2 : 3;
                ctx.ic_gpstime->compress(multi * ctx.last_gpstime_diff[last_seq],
                                         curr_gpstime_diff, gps_ctx);
            } else {
                enc.encodeSymbol(*ctx.m_gpstime_multi, GPSTIME_MULTI);
                ctx.ic_gpstime->compress(GPSTIME_MULTI * ctx.last_gpstime_diff[last_seq],
                                         curr_gpstime_diff, 4);
                ++ctx.multi_extreme_counter[last_seq];
                if (ctx.multi_extreme_counter[last_seq] > 3) {
                    ctx.last_gpstime_diff[last_seq] = curr_gpstime_diff;
                    ctx.multi_extreme_counter[last_seq] = 0;
                }
            }
        } else if (multi < 0) {
            if (multi > GPSTIME_MULTI_MINUS) {
                enc.encodeSymbol(*ctx.m_gpstime_multi, GPSTIME_MULTI - multi);
                ctx.ic_gpstime->compress(multi * ctx.last_gpstime_diff[last_seq],
                                         curr_gpstime_diff, 5);
            } else {
                enc.encodeSymbol(*ctx.m_gpstime_multi, GPSTIME_MULTI - GPSTIME_MULTI_MINUS);
                ctx.ic_gpstime->compress(GPSTIME_MULTI_MINUS * ctx.last_gpstime_diff[last_seq],
                                         curr_gpstime_diff, 6);
                ++ctx.multi_extreme_counter[last_seq];
                if (ctx.multi_extreme_counter[last_seq] > 3) {
                    ctx.last_gpstime_diff[last_seq] = curr_gpstime_diff;
                    ctx.multi_extreme_counter[last_seq] = 0;
                }
            }
        } else {
            enc.encodeSymbol(*ctx.m_gpstime_multi, 0);
            ctx.ic_gpstime->compress(0, curr_gpstime_diff, 7);
            ctx.multi_extreme_counter[last_seq]++;
            if (ctx.multi_extreme_counter[last_seq] > 3) {
                ctx.last_gpstime_diff[last_seq] = curr_gpstime_diff;
                ctx.multi_extreme_counter[last_seq] = 0;
            }
        }

        if (multi >= 1 && multi < GPSTIME_MULTI) {
            ctx.last_gpstime_diff[last_seq] = curr_gpstime_diff;
        }
    } else {
        enc.encodeSymbol(*ctx.m_gpstime_multi, GPSTIME_MULTI_CODE_FULL);
        ctx.ic_gpstime->compress(static_cast<i32>(ctx.last_gpstime[last_seq].u >> 32),
                                 static_cast<i32>(this_gpstime.u >> 32), 8);
        enc.writeInt(static_cast<u32>(this_gpstime.u));
        ctx.next_gpstime_seq = (ctx.next_gpstime_seq + 1) & 3;
        ctx.last_gpstime_seq = ctx.next_gpstime_seq;
        ctx.last_gpstime_diff[ctx.last_gpstime_seq] = 0;
        ctx.multi_extreme_counter[ctx.last_gpstime_seq] = 0;
    }

    ctx.last_gpstime[ctx.last_gpstime_seq].i = this_gpstime.i;
}

void Point14Compressor::chunk_sizes() {
    for (u32 i = 0; i < POINT14_NUM_LAYERS; ++i) {
        layer_enc_[i].done();
    }

    for (u32 i = 0; i < POINT14_NUM_LAYERS; ++i) {
        u32 size = layer_out_[i].size();
        main_out_->put_bytes(reinterpret_cast<const u8*>(&size), 4);
    }
}

void Point14Compressor::chunk_bytes() {
    for (u32 i = 0; i < POINT14_NUM_LAYERS; ++i) {
        if (layer_out_[i].size() > 0) {
            main_out_->put_bytes(layer_out_[i].data(), layer_out_[i].size());
        }
    }
}

} // namespace picolaz
