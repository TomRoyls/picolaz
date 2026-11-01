// Bit-exact port of the LASzip POINT14 v3 item compressors (write + read side)
// for byte-level interoperability with LASzip and LAZperf. The algorithm and
// its context parameters follow the LASzip implementation (rapidlasso GmbH,
// Apache-2.0); this is an independent MPL-2.0 adaptation for picolaz.
//
// Items are the standard 30-byte LAS 1.4 point record prefix:
//   X i32 @0, Y i32 @4, Z i32 @8, intensity u16 @12,
//   byte14 = return_number | (number_of_returns << 4),
//   byte15 = classification_flags | (scanner_channel << 4) |
//            (scan_direction << 6) | (edge_of_flight << 7),
//   classification u8 @16, user_data u8 @17, scan_angle i16 @18,
//   point_source u16 @20, gps_time f64 @22.

#include "picolaz/laz14_v3.hpp"

#include <cstring>

namespace picolaz::v3 {

namespace {

using Record = const std::uint8_t*;

inline std::int32_t rd_i32(Record r, int o) {
  std::int32_t v;
  std::memcpy(&v, r + o, 4);
  return v;
}
inline std::uint16_t rd_u16(Record r, int o) {
  std::uint16_t v;
  std::memcpy(&v, r + o, 2);
  return v;
}
inline std::int16_t rd_i16(Record r, int o) {
  std::int16_t v;
  std::memcpy(&v, r + o, 2);
  return v;
}
inline std::int64_t rd_i64(Record r, int o) {
  std::int64_t v;
  std::memcpy(&v, r + o, 8);
  return v;
}
inline void wr_i32(std::uint8_t* r, int o, std::int32_t v) { std::memcpy(r + o, &v, 4); }
inline void wr_u16(std::uint8_t* r, int o, std::uint16_t v) { std::memcpy(r + o, &v, 2); }
inline void wr_i16(std::uint8_t* r, int o, std::int16_t v) { std::memcpy(r + o, &v, 2); }
inline void wr_i64(std::uint8_t* r, int o, std::int64_t v) { std::memcpy(r + o, &v, 8); }

inline std::uint32_t ret_num(Record item) { return item[14] & 0x0F; }
inline std::uint32_t num_ret(Record item) { return (item[14] >> 4) & 0x0F; }
inline std::uint32_t class_flags(Record item) { return item[15] & 0x0F; }
inline std::uint32_t scanner_channel(Record item) { return (item[15] >> 4) & 0x03; }
inline std::uint32_t scan_dir(Record item) { return (item[15] >> 6) & 0x01; }
inline std::uint32_t eof_line(Record item) { return (item[15] >> 7) & 0x01; }
inline std::uint32_t u32_zero_bit_0(std::uint32_t v) { return v & 0xFFFFFFFE; }

inline void set_channel(std::uint8_t* item, std::uint32_t channel) {
  item[15] = static_cast<std::uint8_t>((item[15] & 0xCF) | (channel << 4));
}

inline void set_flags(std::uint8_t* item, std::uint32_t flags) {
  item[15] = static_cast<std::uint8_t>((item[15] & 0x30) | (flags & 0x0F) |
                                       ((flags & 0x10) << 2) | ((flags & 0x20) << 2));
}

}

std::int32_t quantize_i32(double v) {
  return static_cast<std::int32_t>(v >= 0 ? v + 0.5 : v - 0.5);
}

void Point14CompressorV3::create_and_init_models_and_compressors(
    std::uint32_t context, const std::uint8_t* item) {
  Point14Context* ctx = &contexts_[context];

  if (!ctx->created) {
    for (int i = 0; i < 8; ++i)
      ctx->m_changed_values[i] = std::make_unique<ArithmeticModel>();
    ctx->m_scanner_channel = std::make_unique<ArithmeticModel>();
    for (int i = 0; i < 16; ++i) {
      ctx->m_number_of_returns[i] = std::make_unique<ArithmeticModel>();
      ctx->m_return_number[i] = std::make_unique<ArithmeticModel>();
    }
    ctx->m_return_number_gps_same = std::make_unique<ArithmeticModel>();
    for (int i = 0; i < 64; ++i) {
      ctx->m_classification[i] = std::make_unique<ArithmeticModel>();
      ctx->m_flags[i] = std::make_unique<ArithmeticModel>();
      ctx->m_user_data[i] = std::make_unique<ArithmeticModel>();
    }
    ctx->m_gpstime_multi = std::make_unique<ArithmeticModel>();
    ctx->m_gpstime_0diff = std::make_unique<ArithmeticModel>();

    ctx->ic_dX = std::make_unique<IntegerCompressor>(&enc_[LAYER_CHANNEL_RETURNS_XY], 32, 2);
    ctx->ic_dY = std::make_unique<IntegerCompressor>(&enc_[LAYER_CHANNEL_RETURNS_XY], 32, 22);
    ctx->ic_Z = std::make_unique<IntegerCompressor>(&enc_[LAYER_Z], 32, 20);
    ctx->ic_intensity = std::make_unique<IntegerCompressor>(&enc_[LAYER_INTENSITY], 16, 4);
    ctx->ic_scan_angle = std::make_unique<IntegerCompressor>(&enc_[LAYER_SCAN_ANGLE], 16, 2);
    ctx->ic_point_source =
        std::make_unique<IntegerCompressor>(&enc_[LAYER_POINT_SOURCE], 16, 1);
    ctx->ic_gpstime = std::make_unique<IntegerCompressor>(&enc_[LAYER_GPS_TIME], 32, 9);
    ctx->created = true;
  }

  for (int i = 0; i < 8; ++i) ctx->m_changed_values[i]->init(128, true);
  ctx->m_scanner_channel->init(3, true);
  for (int i = 0; i < 16; ++i) {
    ctx->m_number_of_returns[i]->init(16, true);
    ctx->m_return_number[i]->init(16, true);
  }
  ctx->m_return_number_gps_same->init(13, true);
  for (int i = 0; i < 64; ++i) {
    ctx->m_classification[i]->init(256, true);
    ctx->m_flags[i]->init(64, true);
    ctx->m_user_data[i]->init(256, true);
  }
  ctx->m_gpstime_multi->init(kGpsTimeMultiTotal, true);
  ctx->m_gpstime_0diff->init(5, true);

  ctx->ic_dX->initCompressor();
  ctx->ic_dY->initCompressor();
  for (int i = 0; i < 12; ++i) {
    ctx->last_X_diff_median5[i].init();
    ctx->last_Y_diff_median5[i].init();
  }
  ctx->ic_Z->initCompressor();
  for (int i = 0; i < 8; ++i) ctx->last_Z[i] = rd_i32(item, 8);
  ctx->ic_intensity->initCompressor();
  for (int i = 0; i < 8; ++i) ctx->last_intensity[i] = rd_u16(item, 12);
  ctx->ic_scan_angle->initCompressor();
  ctx->ic_point_source->initCompressor();
  ctx->ic_gpstime->initCompressor();

  ctx->gps_last = 0;
  ctx->gps_next = 0;
  for (int i = 0; i < 4; ++i) {
    ctx->last_gpstime_diff[i] = 0;
    ctx->multi_extreme_counter[i] = 0;
  }
  ctx->last_gpstime[0] = rd_i64(item, 22);
  ctx->last_gpstime[1] = 0;
  ctx->last_gpstime[2] = 0;
  ctx->last_gpstime[3] = 0;

  std::memcpy(ctx->last_item, item, kPoint14Size);
  ctx->last_gps_time_change = false;
  ctx->unused = false;
}

void Point14CompressorV3::init(const std::uint8_t* first_item, std::uint32_t context) {
  current_context_ = context;

  for (int i = 0; i < kPoint14Layers; ++i) {
    layer_out_[i] = ByteStreamOut();
    enc_[i].init(&layer_out_[i]);
    changed_[i] = false;
  }
  changed_[LAYER_CHANNEL_RETURNS_XY] = true;
  changed_[LAYER_Z] = true;

  create_and_init_models_and_compressors(context, first_item);
}

void Point14CompressorV3::write(const std::uint8_t* item, std::uint32_t context) {
  Point14Context* ctx = &contexts_[current_context_];
  const std::uint8_t* last_item = ctx->last_item;

  std::int32_t lpr = (ret_num(last_item) == 1 ? 1 : 0);
  lpr += (ret_num(last_item) >= num_ret(last_item) ? 2 : 0);
  lpr += (ctx->last_gps_time_change ? 4 : 0);

  std::uint32_t channel = scanner_channel(item);
  if (channel != current_context_) {
    if (!contexts_[channel].unused) last_item = contexts_[channel].last_item;
  }

  bool point_source_change = rd_u16(item, 20) != rd_u16(last_item, 20);
  bool gps_time_change = rd_i64(item, 22) != rd_i64(last_item, 22);
  bool scan_angle_change = rd_i16(item, 18) != rd_i16(last_item, 18);

  std::uint32_t last_n = num_ret(last_item);
  std::uint32_t last_r = ret_num(last_item);
  std::uint32_t n = num_ret(item);
  std::uint32_t r = ret_num(item);

  std::int32_t changed_values = ((channel != current_context_) << 6) |
                                (point_source_change << 5) | (gps_time_change << 4) |
                                (scan_angle_change << 3) | ((n != last_n) << 2);
  if (r != last_r) {
    if (r == ((last_r + 1) % 16))
      changed_values |= 1;
    else if (r == ((last_r + 15) % 16))
      changed_values |= 2;
    else
      changed_values |= 3;
  }

  enc_[LAYER_CHANNEL_RETURNS_XY].encodeSymbol(*ctx->m_changed_values[lpr], changed_values);

  if (changed_values & (1 << 6)) {
    std::int32_t diff = static_cast<std::int32_t>(channel) -
                        static_cast<std::int32_t>(current_context_);
    if (diff > 0) {
      enc_[LAYER_CHANNEL_RETURNS_XY].encodeSymbol(*ctx->m_scanner_channel, diff - 1);
    } else {
      enc_[LAYER_CHANNEL_RETURNS_XY].encodeSymbol(*ctx->m_scanner_channel, diff + 3);
    }
    if (contexts_[channel].unused) {
      create_and_init_models_and_compressors(channel, contexts_[current_context_].last_item);
    }
    current_context_ = channel;
    context = current_context_;
    ctx = &contexts_[current_context_];
    last_item = ctx->last_item;
  }

  if (changed_values & (1 << 2)) {
    enc_[LAYER_CHANNEL_RETURNS_XY].encodeSymbol(*ctx->m_number_of_returns[last_n], n);
  }

  if ((changed_values & 3) == 3) {
    if (gps_time_change) {
      enc_[LAYER_CHANNEL_RETURNS_XY].encodeSymbol(*ctx->m_return_number[last_r], r);
    } else {
      std::int32_t diff = static_cast<std::int32_t>(r) - static_cast<std::int32_t>(last_r);
      if (diff > 1) {
        enc_[LAYER_CHANNEL_RETURNS_XY].encodeSymbol(*ctx->m_return_number_gps_same, diff - 2);
      } else {
        enc_[LAYER_CHANNEL_RETURNS_XY].encodeSymbol(*ctx->m_return_number_gps_same, diff + 14);
      }
    }
  }

  std::uint32_t m = number_return_map_6ctx[n][r];
  std::uint32_t l = number_return_level_8ctx[n][r];

  std::int32_t cpr = (r == 1 ? 2 : 0);
  cpr += (r >= n ? 1 : 0);

  std::uint32_t gps = gps_time_change ? 1 : 0;
  std::uint32_t k_bits;
  std::int32_t median, diff;

  median = ctx->last_X_diff_median5[(m << 1) | gps].get();
  diff = rd_i32(item, 0) - rd_i32(last_item, 0);
  ctx->ic_dX->compress(median, diff, n == 1);
  ctx->last_X_diff_median5[(m << 1) | gps].add(diff);

  k_bits = ctx->ic_dX->getK();
  median = ctx->last_Y_diff_median5[(m << 1) | gps].get();
  diff = rd_i32(item, 4) - rd_i32(last_item, 4);
  ctx->ic_dY->compress(median, diff,
                       (n == 1) + (k_bits < 20 ? u32_zero_bit_0(k_bits) : 20));
  ctx->last_Y_diff_median5[(m << 1) | gps].add(diff);

  k_bits = (ctx->ic_dX->getK() + ctx->ic_dY->getK()) / 2;
  ctx->ic_Z->compress(ctx->last_Z[l], rd_i32(item, 8),
                      (n == 1) + (k_bits < 18 ? u32_zero_bit_0(k_bits) : 18));
  ctx->last_Z[l] = rd_i32(item, 8);

  std::uint32_t last_classification = last_item[16];
  std::uint32_t classification = item[16];
  if (classification != last_classification) changed_[LAYER_CLASSIFICATION] = true;
  std::int32_t ccc = static_cast<std::int32_t>((last_classification & 0x1F) << 1) +
                     (cpr == 3 ? 1 : 0);
  enc_[LAYER_CLASSIFICATION].encodeSymbol(*ctx->m_classification[ccc], classification);

  std::uint32_t last_flags = (eof_line(last_item) << 5) | (scan_dir(last_item) << 4) |
                             class_flags(last_item);
  std::uint32_t flags = (eof_line(item) << 5) | (scan_dir(item) << 4) | class_flags(item);
  if (flags != last_flags) changed_[LAYER_FLAGS] = true;
  enc_[LAYER_FLAGS].encodeSymbol(*ctx->m_flags[last_flags], flags);

  if (rd_u16(item, 12) != rd_u16(last_item, 12)) changed_[LAYER_INTENSITY] = true;
  ctx->ic_intensity->compress(ctx->last_intensity[(cpr << 1) | gps], rd_u16(item, 12), cpr);
  ctx->last_intensity[(cpr << 1) | gps] = rd_u16(item, 12);

  if (scan_angle_change) {
    changed_[LAYER_SCAN_ANGLE] = true;
    ctx->ic_scan_angle->compress(rd_i16(last_item, 18), rd_i16(item, 18), gps);
  }

  if (item[17] != last_item[17]) changed_[LAYER_USER_DATA] = true;
  enc_[LAYER_USER_DATA].encodeSymbol(*ctx->m_user_data[last_item[17] / 4], item[17]);

  if (point_source_change) {
    changed_[LAYER_POINT_SOURCE] = true;
    ctx->ic_point_source->compress(rd_u16(last_item, 20), rd_u16(item, 20));
  }

  if (gps_time_change) {
    changed_[LAYER_GPS_TIME] = true;
    write_gps_time(rd_i64(item, 22));
  }

  ctx->last_gps_time_change = gps_time_change;
  std::memcpy(ctx->last_item, item, kPoint14Size);
}

void Point14CompressorV3::write_gps_time(std::int64_t gps_time) {
  Point14Context* ctx = &contexts_[current_context_];
  ArithmeticEncoder& enc = enc_[LAYER_GPS_TIME];
  const std::uint32_t last = ctx->gps_last;

  if (ctx->last_gpstime_diff[last] == 0) {
    std::int64_t curr_diff_64 = gps_time - ctx->last_gpstime[last];
    std::int32_t curr_diff = static_cast<std::int32_t>(curr_diff_64);
    if (curr_diff_64 == static_cast<std::int64_t>(curr_diff)) {
      enc.encodeSymbol(*ctx->m_gpstime_0diff, 0);
      ctx->ic_gpstime->compress(0, curr_diff, 0);
      ctx->last_gpstime_diff[last] = curr_diff;
      ctx->multi_extreme_counter[last] = 0;
    } else {
      bool belongs_to_other = false;
      for (std::uint32_t i = 1; i < 4; ++i) {
        std::int64_t other_diff_64 = gps_time - ctx->last_gpstime[(last + i) & 3];
        std::int32_t other_diff = static_cast<std::int32_t>(other_diff_64);
        if (other_diff_64 == static_cast<std::int64_t>(other_diff)) {
          enc.encodeSymbol(*ctx->m_gpstime_0diff, i + 1);
          ctx->gps_last = (last + i) & 3;
          write_gps_time(gps_time);
          belongs_to_other = true;
          break;
        }
      }
      if (!belongs_to_other) {
        enc.encodeSymbol(*ctx->m_gpstime_0diff, 1);
        ctx->ic_gpstime->compress(static_cast<std::int32_t>(ctx->last_gpstime[last] >> 32),
                                  static_cast<std::int32_t>(gps_time >> 32), 8);
        enc.writeBits(32, static_cast<std::uint32_t>(gps_time));
        ctx->gps_next = (ctx->gps_next + 1) & 3;
        ctx->gps_last = ctx->gps_next;
        ctx->last_gpstime_diff[ctx->gps_last] = 0;
        ctx->multi_extreme_counter[ctx->gps_last] = 0;
      }
    }
    ctx->last_gpstime[ctx->gps_last] = gps_time;
    return;
  }

  std::int64_t curr_diff_64 = gps_time - ctx->last_gpstime[last];
  std::int32_t curr_diff = static_cast<std::int32_t>(curr_diff_64);

  if (curr_diff_64 == static_cast<std::int64_t>(curr_diff)) {
    float multi_f = static_cast<float>(curr_diff) /
                    static_cast<float>(ctx->last_gpstime_diff[last]);
    std::int32_t multi = quantize_i32(multi_f);

    if (multi == 1) {
      enc.encodeSymbol(*ctx->m_gpstime_multi, 1);
      ctx->ic_gpstime->compress(ctx->last_gpstime_diff[last], curr_diff, 1);
      ctx->multi_extreme_counter[last] = 0;
    } else if (multi > 0) {
      if (multi < kGpsTimeMulti) {
        enc.encodeSymbol(*ctx->m_gpstime_multi, multi);
        if (multi < 10) {
          ctx->ic_gpstime->compress(multi * ctx->last_gpstime_diff[last], curr_diff, 2);
        } else {
          ctx->ic_gpstime->compress(multi * ctx->last_gpstime_diff[last], curr_diff, 3);
        }
      } else {
        enc.encodeSymbol(*ctx->m_gpstime_multi, kGpsTimeMulti);
        ctx->ic_gpstime->compress(kGpsTimeMulti * ctx->last_gpstime_diff[last], curr_diff,
                                  4);
        ctx->multi_extreme_counter[last]++;
        if (ctx->multi_extreme_counter[last] > 3) {
          ctx->last_gpstime_diff[last] = curr_diff;
          ctx->multi_extreme_counter[last] = 0;
        }
      }
    } else if (multi < 0) {
      if (multi > kGpsTimeMultiMinus) {
        enc.encodeSymbol(*ctx->m_gpstime_multi, kGpsTimeMulti - multi);
        ctx->ic_gpstime->compress(multi * ctx->last_gpstime_diff[last], curr_diff, 5);
      } else {
        enc.encodeSymbol(*ctx->m_gpstime_multi, kGpsTimeMulti - kGpsTimeMultiMinus);
        ctx->ic_gpstime->compress(kGpsTimeMultiMinus * ctx->last_gpstime_diff[last],
                                  curr_diff, 6);
        ctx->multi_extreme_counter[last]++;
        if (ctx->multi_extreme_counter[last] > 3) {
          ctx->last_gpstime_diff[last] = curr_diff;
          ctx->multi_extreme_counter[last] = 0;
        }
      }
    } else {
      enc.encodeSymbol(*ctx->m_gpstime_multi, 0);
      ctx->ic_gpstime->compress(0, curr_diff, 7);
      ctx->multi_extreme_counter[last]++;
      if (ctx->multi_extreme_counter[last] > 3) {
        ctx->last_gpstime_diff[last] = curr_diff;
        ctx->multi_extreme_counter[last] = 0;
      }
    }
    ctx->last_gpstime[last] = gps_time;
    return;
  }

  bool belongs_to_other = false;
  for (std::uint32_t i = 1; i < 4; ++i) {
    std::int64_t other_diff_64 = gps_time - ctx->last_gpstime[(last + i) & 3];
    std::int32_t other_diff = static_cast<std::int32_t>(other_diff_64);
    if (other_diff_64 == static_cast<std::int64_t>(other_diff)) {
      enc.encodeSymbol(*ctx->m_gpstime_multi, kGpsTimeMultiCodeFull + i);
      ctx->gps_last = (last + i) & 3;
      write_gps_time(gps_time);
      belongs_to_other = true;
      break;
    }
  }
  if (!belongs_to_other) {
    enc.encodeSymbol(*ctx->m_gpstime_multi, kGpsTimeMultiCodeFull);
    ctx->ic_gpstime->compress(static_cast<std::int32_t>(ctx->last_gpstime[last] >> 32),
                              static_cast<std::int32_t>(gps_time >> 32), 8);
    enc.writeBits(32, static_cast<std::uint32_t>(gps_time));
    ctx->gps_next = (ctx->gps_next + 1) & 3;
    ctx->gps_last = ctx->gps_next;
    ctx->last_gpstime_diff[ctx->gps_last] = 0;
    ctx->multi_extreme_counter[ctx->gps_last] = 0;
    ctx->last_gpstime[ctx->gps_last] = gps_time;
  }
}

void Point14CompressorV3::finish_chunk(std::uint32_t sizes[kPoint14Layers]) {
  enc_[LAYER_CHANNEL_RETURNS_XY].done();
  enc_[LAYER_Z].done();
  if (changed_[LAYER_CLASSIFICATION]) enc_[LAYER_CLASSIFICATION].done();
  if (changed_[LAYER_FLAGS]) enc_[LAYER_FLAGS].done();
  if (changed_[LAYER_INTENSITY]) enc_[LAYER_INTENSITY].done();
  if (changed_[LAYER_SCAN_ANGLE]) enc_[LAYER_SCAN_ANGLE].done();
  if (changed_[LAYER_USER_DATA]) enc_[LAYER_USER_DATA].done();
  if (changed_[LAYER_POINT_SOURCE]) enc_[LAYER_POINT_SOURCE].done();
  if (changed_[LAYER_GPS_TIME]) enc_[LAYER_GPS_TIME].done();

  sizes[LAYER_CHANNEL_RETURNS_XY] = layer_out_[LAYER_CHANNEL_RETURNS_XY].size();
  sizes[LAYER_Z] = layer_out_[LAYER_Z].size();
  for (int layer = LAYER_CLASSIFICATION; layer <= LAYER_GPS_TIME; ++layer) {
    sizes[layer] = changed_[layer] ? layer_out_[layer].size() : 0;
  }
}

void Point14CompressorV3::write_layer_bytes(ByteStreamOut* out) {
  for (int i = 0; i < kPoint14Layers; ++i) {
    if (!changed_[i]) continue;
    out->put_bytes(layer_out_[i].data(), layer_out_[i].size());
  }
}

void Point14DecompressorV3::create_and_init_models_and_decompressors(
    std::uint32_t context, const std::uint8_t* item) {
  Point14Context* ctx = &contexts_[context];

  if (!ctx->created) {
    for (int i = 0; i < 8; ++i)
      ctx->m_changed_values[i] = std::make_unique<ArithmeticModel>();
    ctx->m_scanner_channel = std::make_unique<ArithmeticModel>();
    for (int i = 0; i < 16; ++i) {
      ctx->m_number_of_returns[i] = std::make_unique<ArithmeticModel>();
      ctx->m_return_number[i] = std::make_unique<ArithmeticModel>();
    }
    ctx->m_return_number_gps_same = std::make_unique<ArithmeticModel>();
    for (int i = 0; i < 64; ++i) {
      ctx->m_classification[i] = std::make_unique<ArithmeticModel>();
      ctx->m_flags[i] = std::make_unique<ArithmeticModel>();
      ctx->m_user_data[i] = std::make_unique<ArithmeticModel>();
    }
    ctx->m_gpstime_multi = std::make_unique<ArithmeticModel>();
    ctx->m_gpstime_0diff = std::make_unique<ArithmeticModel>();

    ctx->ic_dX = std::make_unique<IntegerCompressor>(&dec_[LAYER_CHANNEL_RETURNS_XY], 32, 2);
    ctx->ic_dY = std::make_unique<IntegerCompressor>(&dec_[LAYER_CHANNEL_RETURNS_XY], 32, 22);
    ctx->ic_Z = std::make_unique<IntegerCompressor>(&dec_[LAYER_Z], 32, 20);
    ctx->ic_intensity = std::make_unique<IntegerCompressor>(&dec_[LAYER_INTENSITY], 16, 4);
    ctx->ic_scan_angle = std::make_unique<IntegerCompressor>(&dec_[LAYER_SCAN_ANGLE], 16, 2);
    ctx->ic_point_source =
        std::make_unique<IntegerCompressor>(&dec_[LAYER_POINT_SOURCE], 16, 1);
    ctx->ic_gpstime = std::make_unique<IntegerCompressor>(&dec_[LAYER_GPS_TIME], 32, 9);
    ctx->created = true;
  }

  for (int i = 0; i < 8; ++i) ctx->m_changed_values[i]->init(128, false);
  ctx->m_scanner_channel->init(3, false);
  for (int i = 0; i < 16; ++i) {
    ctx->m_number_of_returns[i]->init(16, false);
    ctx->m_return_number[i]->init(16, false);
  }
  ctx->m_return_number_gps_same->init(13, false);
  for (int i = 0; i < 64; ++i) {
    ctx->m_classification[i]->init(256, false);
    ctx->m_flags[i]->init(64, false);
    ctx->m_user_data[i]->init(256, false);
  }
  ctx->m_gpstime_multi->init(kGpsTimeMultiTotal, false);
  ctx->m_gpstime_0diff->init(5, false);

  ctx->ic_dX->initDecompressor();
  ctx->ic_dY->initDecompressor();
  for (int i = 0; i < 12; ++i) {
    ctx->last_X_diff_median5[i].init();
    ctx->last_Y_diff_median5[i].init();
  }
  ctx->ic_Z->initDecompressor();
  for (int i = 0; i < 8; ++i) ctx->last_Z[i] = rd_i32(item, 8);
  ctx->ic_intensity->initDecompressor();
  for (int i = 0; i < 8; ++i) ctx->last_intensity[i] = rd_u16(item, 12);
  ctx->ic_scan_angle->initDecompressor();
  ctx->ic_point_source->initDecompressor();
  ctx->ic_gpstime->initDecompressor();

  ctx->gps_last = 0;
  ctx->gps_next = 0;
  for (int i = 0; i < 4; ++i) {
    ctx->last_gpstime_diff[i] = 0;
    ctx->multi_extreme_counter[i] = 0;
  }
  ctx->last_gpstime[0] = rd_i64(item, 22);
  ctx->last_gpstime[1] = 0;
  ctx->last_gpstime[2] = 0;
  ctx->last_gpstime[3] = 0;

  std::memcpy(ctx->last_item, item, kPoint14Size);
  ctx->last_gps_time_change = false;
  ctx->unused = false;
}

void Point14DecompressorV3::init(const std::uint32_t* sizes,
                                 const std::uint8_t* layer_bytes,
                                 std::uint32_t layer_bytes_len,
                                 const std::uint8_t* first_item,
                                 std::uint32_t context) {
  current_context_ = context;

  std::uint32_t off = 0;
  for (int i = 0; i < kPoint14Layers; ++i) {
    layer_avail_[i] = sizes[i] != 0;
    layer_buf_[i].assign(layer_bytes + off, layer_bytes + off + sizes[i]);
    off += sizes[i];
    if (sizes[i]) {
      layer_in_[i].init(layer_buf_[i].data(),
                        static_cast<std::uint32_t>(layer_buf_[i].size()));
      dec_[i].init(&layer_in_[i]);
    }
  }

  create_and_init_models_and_decompressors(context, first_item);
}

void Point14DecompressorV3::read(std::uint8_t* item, std::uint32_t& context) {
  Point14Context* ctx = &contexts_[current_context_];
  const std::uint8_t* last_item = ctx->last_item;
  std::memcpy(item, last_item, kPoint14Size);

  std::int32_t lpr = (ret_num(last_item) == 1 ? 1 : 0);
  lpr += (ret_num(last_item) >= num_ret(last_item) ? 2 : 0);
  lpr += (ctx->last_gps_time_change ? 4 : 0);

  std::int32_t changed_values = static_cast<std::int32_t>(
      dec_[LAYER_CHANNEL_RETURNS_XY].decodeSymbol(*ctx->m_changed_values[lpr]));

  bool gps_time_change = (changed_values & (1 << 4)) != 0;
  bool scan_angle_change = (changed_values & (1 << 3)) != 0;
  bool point_source_change = (changed_values & (1 << 5)) != 0;

  if (changed_values & (1 << 6)) {
    std::uint32_t sym =
        dec_[LAYER_CHANNEL_RETURNS_XY].decodeSymbol(*ctx->m_scanner_channel);
    std::uint32_t channel = (current_context_ + sym + 1) & 3;
    if (contexts_[channel].unused) {
      create_and_init_models_and_decompressors(channel,
                                               contexts_[current_context_].last_item);
    }
    current_context_ = channel;
    context = current_context_;
    ctx = &contexts_[current_context_];
    last_item = ctx->last_item;
    std::memcpy(item, last_item, kPoint14Size);
    set_channel(item, channel);
  }

  std::uint32_t last_n = num_ret(item);
  std::uint32_t last_r = ret_num(item);
  std::uint32_t n = last_n;
  std::uint32_t r = last_r;

  if (changed_values & (1 << 2)) {
    n = dec_[LAYER_CHANNEL_RETURNS_XY].decodeSymbol(*ctx->m_number_of_returns[last_n]);
  }

  if ((changed_values & 3) == 3) {
    if (gps_time_change) {
      r = dec_[LAYER_CHANNEL_RETURNS_XY].decodeSymbol(*ctx->m_return_number[last_r]);
    } else {
      std::int32_t sym = static_cast<std::int32_t>(
          dec_[LAYER_CHANNEL_RETURNS_XY].decodeSymbol(*ctx->m_return_number_gps_same));
      r = (last_r + static_cast<std::uint32_t>(sym) + 2) & 0x0F;
    }
  } else if ((changed_values & 3) == 1) {
    r = (last_r + 1) & 0x0F;
  } else if ((changed_values & 3) == 2) {
    r = (last_r + 15) & 0x0F;
  }

  item[14] = static_cast<std::uint8_t>(r | (n << 4));

  std::uint32_t m = number_return_map_6ctx[n][r];
  std::uint32_t l = number_return_level_8ctx[n][r];

  std::int32_t cpr = (r == 1 ? 2 : 0);
  cpr += (r >= n ? 1 : 0);
  std::uint32_t gps = gps_time_change ? 1 : 0;

  std::uint32_t k_bits;
  std::int32_t median, diff;

  median = ctx->last_X_diff_median5[(m << 1) | gps].get();
  diff = ctx->ic_dX->decompress(median, n == 1);
  ctx->last_X_diff_median5[(m << 1) | gps].add(diff);
  wr_i32(item, 0, rd_i32(last_item, 0) + diff);

  k_bits = ctx->ic_dX->getK();
  median = ctx->last_Y_diff_median5[(m << 1) | gps].get();
  diff = ctx->ic_dY->decompress(median,
                                (n == 1) + (k_bits < 20 ? u32_zero_bit_0(k_bits) : 20));
  ctx->last_Y_diff_median5[(m << 1) | gps].add(diff);
  wr_i32(item, 4, rd_i32(last_item, 4) + diff);

  k_bits = (ctx->ic_dX->getK() + ctx->ic_dY->getK()) / 2;
  wr_i32(item, 8,
         ctx->ic_Z->decompress(ctx->last_Z[l],
                               (n == 1) + (k_bits < 18 ? u32_zero_bit_0(k_bits) : 18)));
  ctx->last_Z[l] = rd_i32(item, 8);

  if (layer_avail_[LAYER_CLASSIFICATION]) {
    std::int32_t ccc =
        static_cast<std::int32_t>((last_item[16] & 0x1F) << 1) + (cpr == 3 ? 1 : 0);
    item[16] = static_cast<std::uint8_t>(
        dec_[LAYER_CLASSIFICATION].decodeSymbol(*ctx->m_classification[ccc]));
  }

  if (layer_avail_[LAYER_FLAGS]) {
    std::uint32_t last_flags =
        (eof_line(item) << 5) | (scan_dir(item) << 4) | class_flags(item);
    std::uint32_t flags = dec_[LAYER_FLAGS].decodeSymbol(*ctx->m_flags[last_flags]);
    set_flags(item, flags);
  }

  if (layer_avail_[LAYER_INTENSITY]) {
    wr_u16(item, 12,
           ctx->ic_intensity->decompress(ctx->last_intensity[(cpr << 1) | gps], cpr));
    ctx->last_intensity[(cpr << 1) | gps] = rd_u16(item, 12);
  }

  if (scan_angle_change && layer_avail_[LAYER_SCAN_ANGLE]) {
    wr_i16(item, 18, ctx->ic_scan_angle->decompress(rd_i16(item, 18), gps));
  }

  if (layer_avail_[LAYER_USER_DATA]) {
    item[17] = static_cast<std::uint8_t>(
        dec_[LAYER_USER_DATA].decodeSymbol(*ctx->m_user_data[item[17] / 4]));
  }

  if (point_source_change && layer_avail_[LAYER_POINT_SOURCE]) {
    wr_u16(item, 20, ctx->ic_point_source->decompress(rd_u16(item, 20)));
  }

  if (gps_time_change && layer_avail_[LAYER_GPS_TIME]) {
    wr_i64(item, 22, read_gps_time());
  }

  ctx->last_gps_time_change = gps_time_change;
  std::memcpy(ctx->last_item, item, kPoint14Size);
}

std::int64_t Point14DecompressorV3::read_gps_time() {
  Point14Context* ctx = &contexts_[current_context_];
  ArithmeticDecoder& dec = dec_[LAYER_GPS_TIME];
  const std::uint32_t last = ctx->gps_last;

  auto start_new_sequence = [&]() -> std::int64_t {
    std::int32_t hi = ctx->ic_gpstime->decompress(
        static_cast<std::int32_t>(ctx->last_gpstime[last] >> 32), 8);
    std::uint32_t lo = dec.readBits(32);
    std::int64_t gps_time = (static_cast<std::int64_t>(hi) << 32) |
                            static_cast<std::int64_t>(lo);
    ctx->gps_next = (ctx->gps_next + 1) & 3;
    ctx->gps_last = ctx->gps_next;
    ctx->last_gpstime_diff[ctx->gps_last] = 0;
    ctx->multi_extreme_counter[ctx->gps_last] = 0;
    ctx->last_gpstime[ctx->gps_last] = gps_time;
    return gps_time;
  };

  if (ctx->last_gpstime_diff[last] == 0) {
    std::uint32_t sym = dec.decodeSymbol(*ctx->m_gpstime_0diff);
    if (sym == 0) {
      std::int32_t curr_diff = ctx->ic_gpstime->decompress(0, 0);
      ctx->last_gpstime_diff[last] = curr_diff;
      ctx->multi_extreme_counter[last] = 0;
      ctx->last_gpstime[last] += curr_diff;
      return ctx->last_gpstime[last];
    }
    if (sym > 1 && sym < 5) {
      ctx->gps_last = (last + sym - 1) & 3;
      return read_gps_time();
    }
    return start_new_sequence();
  }

  std::uint32_t sym = dec.decodeSymbol(*ctx->m_gpstime_multi);
  if (sym == kGpsTimeMultiCodeFull) {
    return start_new_sequence();
  }
  if (sym > kGpsTimeMultiCodeFull) {
    ctx->gps_last = (last + sym - kGpsTimeMultiCodeFull) & 3;
    return read_gps_time();
  }

  std::int32_t curr_diff;
  if (sym == 1) {
    curr_diff = ctx->ic_gpstime->decompress(ctx->last_gpstime_diff[last], 1);
    ctx->multi_extreme_counter[last] = 0;
  } else if (sym == 0) {
    curr_diff = ctx->ic_gpstime->decompress(0, 7);
    ctx->multi_extreme_counter[last]++;
    if (ctx->multi_extreme_counter[last] > 3) {
      ctx->last_gpstime_diff[last] = curr_diff;
      ctx->multi_extreme_counter[last] = 0;
    }
  } else if (sym < kGpsTimeMulti) {
    std::int32_t multi = static_cast<std::int32_t>(sym);
    std::uint32_t ic_ctx = multi < 10 ? 2 : 3;
    curr_diff = ctx->ic_gpstime->decompress(multi * ctx->last_gpstime_diff[last], ic_ctx);
  } else if (sym == kGpsTimeMulti) {
    curr_diff =
        ctx->ic_gpstime->decompress(kGpsTimeMulti * ctx->last_gpstime_diff[last], 4);
    ctx->multi_extreme_counter[last]++;
    if (ctx->multi_extreme_counter[last] > 3) {
      ctx->last_gpstime_diff[last] = curr_diff;
      ctx->multi_extreme_counter[last] = 0;
    }
  } else {
    std::int32_t multi = kGpsTimeMulti - static_cast<std::int32_t>(sym);
    bool extreme = multi <= kGpsTimeMultiMinus;
    curr_diff = ctx->ic_gpstime->decompress(multi * ctx->last_gpstime_diff[last],
                                            extreme ? 6 : 5);
    if (extreme) {
      ctx->multi_extreme_counter[last]++;
      if (ctx->multi_extreme_counter[last] > 3) {
        ctx->last_gpstime_diff[last] = curr_diff;
        ctx->multi_extreme_counter[last] = 0;
      }
    }
  }
  ctx->last_gpstime[last] += curr_diff;
  return ctx->last_gpstime[last];
}

}
