// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "picolaz/point14_compressor.hpp"
#include <cstring>
namespace picolaz {

Point14Compressor::Point14Compressor() = default;

void Point14Compressor::init(ByteStreamOut* main_out) {
    main_out_ = main_out;
    first_point_ = true;
    current_context_ = 0;

    for (std::uint32_t i = 0; i < POINT14_NUM_LAYERS; ++i) {
        layer_out_[i].clear();
        layer_enc_[i].init(&layer_out_[i]);
    }

    for (auto& ctx : contexts_) {
        ctx.unused = true;
    }
}

void Point14Compressor::init_context(std::uint32_t sc) {
    auto& ctx = contexts_[sc];
    ctx.unused = false;

    auto& enc_crxy = layer_enc_[LAYER_CHANNEL_RETURNS_XY];
    auto& enc_z = layer_enc_[LAYER_Z];
    auto& enc_flags = layer_enc_[LAYER_FLAGS];
    auto& enc_int = layer_enc_[LAYER_INTENSITY];
    auto& enc_sa = layer_enc_[LAYER_SCAN_ANGLE];
    auto& enc_ps = layer_enc_[LAYER_POINT_SOURCE];
    auto& enc_gps = layer_enc_[LAYER_GPSTIME];

    // Suppress unused warnings (these are used later in write())
    (void)enc_z; (void)enc_flags; (void)enc_int; (void)enc_sa; (void)enc_ps; (void)enc_gps;

    ctx.m_changed_values = std::make_unique<ArithmeticModel[]>(8);
    for (std::uint32_t i = 0; i < 8; ++i) ctx.m_changed_values[i].init(128, true);

    ctx.m_scanner_channel = std::make_unique<ArithmeticModel>();
    ctx.m_scanner_channel->init(3, true);

    ctx.m_number_of_returns = std::make_unique<ArithmeticModel[]>(16);
    for (std::uint32_t i = 0; i < 16; ++i) ctx.m_number_of_returns[i].init(16, true);

    ctx.m_return_number = std::make_unique<ArithmeticModel[]>(16);
    for (std::uint32_t i = 0; i < 16; ++i) ctx.m_return_number[i].init(16, true);

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
    for (std::uint32_t i = 0; i < 64; ++i) ctx.m_classification[i].init(256, true);

    ctx.m_flags = std::make_unique<ArithmeticModel[]>(64);
    for (std::uint32_t i = 0; i < 64; ++i) ctx.m_flags[i].init(64, true);

    ctx.m_user_data = std::make_unique<ArithmeticModel[]>(64);
    for (std::uint32_t i = 0; i < 64; ++i) ctx.m_user_data[i].init(256, true);

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

void Point14Compressor::write(const std::uint8_t* raw_bytes) {
    auto* point = reinterpret_cast<const PackedPoint14*>(raw_bytes);

    if (first_point_) {
        first_point_ = false;

        // Write raw first point to all layers
        for (std::uint32_t i = 0; i < POINT14_NUM_LAYERS; ++i) {
            layer_out_[i].put_bytes(raw_bytes, sizeof(PackedPoint14));
        }

        current_context_ = point->scanner_channel;
        init_context(current_context_);

        auto& ctx = contexts_[current_context_];
        std::memcpy(ctx.last_item, raw_bytes, sizeof(PackedPoint14));
        reinterpret_cast<PackedPoint14*>(ctx.last_item)->gps_time_change = false;

        auto* last = reinterpret_cast<PackedPoint14*>(ctx.last_item);
        ctx.last_gpstime[0].f = last->gps_time;
        for (std::uint32_t i = 0; i < 8; ++i) ctx.last_intensity[i] = last->intensity;
        for (std::uint32_t i = 0; i < 8; ++i) ctx.last_Z[i] = last->Z;
        return;
    }

    auto& enc_crxy = layer_enc_[LAYER_CHANNEL_RETURNS_XY];
    auto& enc_cls = layer_enc_[LAYER_CLASSIFICATION];
    auto& enc_flags = layer_enc_[LAYER_FLAGS];
    auto& enc_ud = layer_enc_[LAYER_USER_DATA];

    // Get last item from current context
    std::uint8_t* last_item = contexts_[current_context_].last_item;
    auto* last = reinterpret_cast<PackedPoint14*>(last_item);

    // Compute lpr from LAST point — matches LASzip exactly
    // single(3) / first(1) / last(2) / intermediate(0)
    std::int32_t lpr = (last->return_number == 1 ? 1 : 0);
    lpr += (last->return_number >= last->number_of_returns ? 2 : 0);
    lpr += (last->gps_time_change ? 4 : 0);

    std::uint32_t scanner_channel = point->scanner_channel;

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

    std::uint32_t last_n = last->number_of_returns;
    std::uint32_t last_r = last->return_number;
    std::uint32_t n = point->number_of_returns;
    std::uint32_t r = point->return_number;

    // Build 7-bit changed_values mask
    std::int32_t changed_values = ((scanner_channel != current_context_) ? (1 << 6) : 0) |
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
        std::int32_t diff = static_cast<std::int32_t>(scanner_channel) - static_cast<std::int32_t>(current_context_);
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
            for (std::uint32_t i = 0; i < 8; ++i) new_ctx.last_intensity[i] = new_last->intensity;
            for (std::uint32_t i = 0; i < 8; ++i) new_ctx.last_Z[i] = new_last->Z;
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
            std::int32_t rdiff = static_cast<std::int32_t>(r) - static_cast<std::int32_t>(last_r);
            if (rdiff > 1) {
                enc_crxy.encodeSymbol(*ctx.m_return_number_gps_same, rdiff - 2);
            } else {
                enc_crxy.encodeSymbol(*ctx.m_return_number_gps_same, rdiff + 16 - 2);
            }
        }
    }

    // Get return map m and return level l
    std::uint32_t m = number_return_map_6ctx[n][r];
    std::uint32_t l = number_return_level_8ctx[n][r];

    // Create single(3)/first(2)/last(1)/intermediate(0) return context
    std::int32_t cpr = (r == 1 ? 2 : 0);
    cpr += (r >= n ? 1 : 0);

    // X coordinate
    std::int32_t median_x = ctx.last_X_diff_median5[(m << 1) | (gps_time_change ? 1 : 0)].get();
    std::int32_t diff_x = point->X - last->X;
    ctx.ic_dX->compress(median_x, diff_x, n == 1 ? 1 : 0);
    ctx.last_X_diff_median5[(m << 1) | (gps_time_change ? 1 : 0)].add(diff_x);

    // Y coordinate
    std::uint32_t k_bits_x = ctx.ic_dX->getK();
    std::int32_t median_y = ctx.last_Y_diff_median5[(m << 1) | (gps_time_change ? 1 : 0)].get();
    std::int32_t diff_y = point->Y - last->Y;
    std::uint32_t y_ctx = (n == 1 ? 1 : 0) + (k_bits_x < 20 ? u32_zero_bit_0(k_bits_x) : 20);
    ctx.ic_dY->compress(median_y, diff_y, y_ctx);
    ctx.last_Y_diff_median5[(m << 1) | (gps_time_change ? 1 : 0)].add(diff_y);

    // Z coordinate
    std::uint32_t k_bits_y = ctx.ic_dY->getK();
    std::uint32_t k_bits = (k_bits_x + k_bits_y) / 2;
    std::uint32_t z_ctx = (n == 1 ? 1 : 0) + (k_bits < 18 ? u32_zero_bit_0(k_bits) : 18);
    ctx.ic_Z->compress(ctx.last_Z[l], point->Z, z_ctx);
    ctx.last_Z[l] = point->Z;

    // Classification
    std::uint32_t cls_ctx = ((last->classification & 0x1F) << 1) + (cpr == 3 ? 1 : 0);
    enc_cls.encodeSymbol(ctx.m_classification[cls_ctx], point->classification);

    // Flags
    std::uint32_t last_flags = (static_cast<std::uint32_t>(last->edge_of_flight_line) << 5) |
                     (static_cast<std::uint32_t>(last->scan_direction_flag) << 4) |
                     static_cast<std::uint32_t>(last->classification_flags);
    std::uint32_t flags_val = (static_cast<std::uint32_t>(point->edge_of_flight_line) << 5) |
                    (static_cast<std::uint32_t>(point->scan_direction_flag) << 4) |
                    static_cast<std::uint32_t>(point->classification_flags);
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
    std::uint32_t ud_ctx = last->user_data / 4;
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

    std::uint32_t last_seq = ctx.last_gpstime_seq;

    if (ctx.last_gpstime_diff[last_seq] == 0) {
        std::int64_t curr_gpstime_diff_64 = this_gpstime.i - ctx.last_gpstime[last_seq].i;
        std::int32_t curr_gpstime_diff = static_cast<std::int32_t>(curr_gpstime_diff_64);

        if (curr_gpstime_diff_64 == static_cast<std::int64_t>(curr_gpstime_diff)) {
            // Fits in 32 bits
            enc.encodeSymbol(*ctx.m_gpstime_0diff, 0);
            ctx.ic_gpstime->compress(0, curr_gpstime_diff, 0);
            ctx.last_gpstime_diff[last_seq] = curr_gpstime_diff;
            ctx.multi_extreme_counter[last_seq] = 0;
        } else {
            // Try other sequences
            for (std::uint32_t i = 1; i < 4; ++i) {
                std::int64_t other_diff = this_gpstime.i - ctx.last_gpstime[(last_seq + i) & 3].i;
                std::int32_t other_diff_32 = static_cast<std::int32_t>(other_diff);
                if (other_diff == static_cast<std::int64_t>(other_diff_32)) {
                    enc.encodeSymbol(*ctx.m_gpstime_0diff, i + 1);
                    ctx.last_gpstime_seq = (last_seq + i) & 3;
                    write_gpstime(point, ctx);
                    return;
                }
            }
            // Start new sequence
            enc.encodeSymbol(*ctx.m_gpstime_0diff, 1);
            ctx.ic_gpstime->compress(static_cast<std::int32_t>(ctx.last_gpstime[last_seq].u >> 32),
                                     static_cast<std::int32_t>(this_gpstime.u >> 32), 8);
            enc.writeInt(static_cast<std::uint32_t>(this_gpstime.u));
            ctx.next_gpstime_seq = (ctx.next_gpstime_seq + 1) & 3;
            ctx.last_gpstime_seq = ctx.next_gpstime_seq;
            ctx.last_gpstime_diff[ctx.last_gpstime_seq] = 0;
            ctx.multi_extreme_counter[ctx.last_gpstime_seq] = 0;
        }
        ctx.last_gpstime[ctx.last_gpstime_seq].i = this_gpstime.i;
        return;
    }

    // last_gpstime_diff != 0
    std::int64_t curr_gpstime_diff_64 = this_gpstime.i - ctx.last_gpstime[last_seq].i;
    std::int32_t curr_gpstime_diff = static_cast<std::int32_t>(curr_gpstime_diff_64);

    if (curr_gpstime_diff_64 == static_cast<std::int64_t>(curr_gpstime_diff)) {
        float multi_f = static_cast<float>(curr_gpstime_diff) /
                      static_cast<float>(ctx.last_gpstime_diff[last_seq]);
        std::int32_t multi = i32_quantize(multi_f);

        if (multi == 1) {
            enc.encodeSymbol(*ctx.m_gpstime_multi, 1);
            ctx.ic_gpstime->compress(ctx.last_gpstime_diff[last_seq], curr_gpstime_diff, 1);
            ctx.multi_extreme_counter[last_seq] = 0;
        } else if (multi > 0) {
            if (multi < GPSTIME_MULTI) {
                enc.encodeSymbol(*ctx.m_gpstime_multi, multi);
                std::uint32_t gps_ctx = (multi < 10) ? 2 : 3;
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
        ctx.ic_gpstime->compress(static_cast<std::int32_t>(ctx.last_gpstime[last_seq].u >> 32),
                                 static_cast<std::int32_t>(this_gpstime.u >> 32), 8);
        enc.writeInt(static_cast<std::uint32_t>(this_gpstime.u));
        ctx.next_gpstime_seq = (ctx.next_gpstime_seq + 1) & 3;
        ctx.last_gpstime_seq = ctx.next_gpstime_seq;
        ctx.last_gpstime_diff[ctx.last_gpstime_seq] = 0;
        ctx.multi_extreme_counter[ctx.last_gpstime_seq] = 0;
    }

    ctx.last_gpstime[ctx.last_gpstime_seq].i = this_gpstime.i;
}

void Point14Compressor::chunk_sizes() {
    for (std::uint32_t i = 0; i < POINT14_NUM_LAYERS; ++i) {
        layer_enc_[i].done();
    }

    for (std::uint32_t i = 0; i < POINT14_NUM_LAYERS; ++i) {
        std::uint32_t size = layer_out_[i].size();
        main_out_->put_bytes(reinterpret_cast<const std::uint8_t*>(&size), 4);
    }
}

void Point14Compressor::chunk_bytes() {
    for (std::uint32_t i = 0; i < POINT14_NUM_LAYERS; ++i) {
        if (layer_out_[i].size() > 0) {
            main_out_->put_bytes(layer_out_[i].data(), layer_out_[i].size());
        }
    }
}

} // namespace picolaz
