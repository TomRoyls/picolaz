// Bit-exact port of the LASzip RGB14 v3 and RGBNIR14 v3 item compressors for
// byte-level interoperability with LASzip and LAZperf. Follows the LASzip
// implementation (rapidlasso GmbH, Apache-2.0); independent MPL-2.0
// adaptation for picolaz.
//
// RGB14 items are the standard 6-byte RGB record suffix (u16 R, G, B);
// RGBNIR14 items are 8 bytes (u16 R, G, B, NIR). A layer that never changed
// during the chunk is stored with size 0 and its values stay at the raw
// first point's.

#include "picolaz/laz14_v3.hpp"

#include <cstring>

namespace picolaz::v3 {

namespace {

inline std::int32_t u8_fold(std::int32_t n) {
  if (n < 0) return n + 256;
  if (n > 255) return n - 256;
  return n;
}

inline std::int32_t u8_clamp(std::int32_t n) {
  if (n <= 0) return 0;
  if (n >= 255) return 255;
  return n;
}

inline std::uint16_t rd_c(const std::uint8_t* item, int i) {
  std::uint16_t v;
  std::memcpy(&v, item + 2 * i, 2);
  return v;
}
inline void wr_c(std::uint8_t* item, int i, std::uint16_t v) {
  std::memcpy(item + 2 * i, &v, 2);
}

inline std::uint32_t rgb_sym(const std::uint16_t* cur, const std::uint16_t* last) {
  std::uint32_t sym = static_cast<std::uint32_t>((last[0] & 0x00FF) != (cur[0] & 0x00FF));
  sym |= static_cast<std::uint32_t>((last[0] & 0xFF00) != (cur[0] & 0xFF00)) << 1;
  sym |= static_cast<std::uint32_t>((last[1] & 0x00FF) != (cur[1] & 0x00FF)) << 2;
  sym |= static_cast<std::uint32_t>((last[1] & 0xFF00) != (cur[1] & 0xFF00)) << 3;
  sym |= static_cast<std::uint32_t>((last[2] & 0x00FF) != (cur[2] & 0x00FF)) << 4;
  sym |= static_cast<std::uint32_t>((last[2] & 0xFF00) != (cur[2] & 0xFF00)) << 5;
  const bool channels_differ =
      ((cur[0] & 0x00FF) != (cur[1] & 0x00FF)) || ((cur[0] & 0x00FF) != (cur[2] & 0x00FF)) ||
      ((cur[0] & 0xFF00) != (cur[1] & 0xFF00)) || ((cur[0] & 0xFF00) != (cur[2] & 0xFF00));
  sym |= static_cast<std::uint32_t>(channels_differ) << 6;
  return sym;
}

}

void Rgb14CompressorV3::create_and_init_models_and_compressors(std::uint32_t context) {
  Rgb14Context* ctxp = &contexts_[context];
  if (!ctxp->created) {
    ctxp->m_byte_used = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_0 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_1 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_2 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_3 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_4 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_5 = std::make_unique<ArithmeticModel>();
    ctxp->created = true;
  }
  ctxp->m_byte_used->init(128, true);
  ctxp->m_rgb_diff_0->init(256, true);
  ctxp->m_rgb_diff_1->init(256, true);
  ctxp->m_rgb_diff_2->init(256, true);
  ctxp->m_rgb_diff_3->init(256, true);
  ctxp->m_rgb_diff_4->init(256, true);
  ctxp->m_rgb_diff_5->init(256, true);
}

void Rgb14CompressorV3::init(const std::uint8_t* first_item, std::uint32_t context) {
  current_context_ = context;
  layer_out_[0] = ByteStreamOut();
  enc_[0].init(&layer_out_[0]);
  changed_ = false;
  create_and_init_models_and_compressors(context);
  std::memcpy(contexts_[context].last_item, first_item, kRgb14Size);
}

void Rgb14CompressorV3::write(const std::uint8_t* item, std::uint32_t context) {
  Rgb14Context* ctxp = &contexts_[current_context_];
  const std::uint8_t* last_item = ctxp->last_item;
  if (current_context_ != context) {
    current_context_ = context;
    ctxp = &contexts_[current_context_];
    if (!ctxp->created) {
      create_and_init_models_and_compressors(current_context_);
      std::memcpy(ctxp->last_item, last_item, kRgb14Size);
    }
    last_item = ctxp->last_item;
  }
  ArithmeticEncoder& enc = enc_[0];

  const std::uint16_t* cur = reinterpret_cast<const std::uint16_t*>(item);
  const std::uint16_t* last = reinterpret_cast<const std::uint16_t*>(last_item);
  const std::uint32_t sym = rgb_sym(cur, last);

  enc.encodeSymbol(*ctxp->m_byte_used, sym);

  std::int32_t diff_l = 0;
  std::int32_t diff_h = 0;
  if (sym & (1 << 0)) {
    diff_l = static_cast<std::int32_t>(cur[0] & 255) - (last[0] & 255);
    enc.encodeSymbol(*ctxp->m_rgb_diff_0, u8_fold(diff_l));
  }
  if (sym & (1 << 1)) {
    diff_h = static_cast<std::int32_t>(cur[0] >> 8) - (last[0] >> 8);
    enc.encodeSymbol(*ctxp->m_rgb_diff_1, u8_fold(diff_h));
  }
  if (sym & (1 << 6)) {
    if (sym & (1 << 2)) {
      std::int32_t corr =
          static_cast<std::int32_t>(cur[1] & 255) - u8_clamp(diff_l + (last[1] & 255));
      enc.encodeSymbol(*ctxp->m_rgb_diff_2, u8_fold(corr));
    }
    if (sym & (1 << 4)) {
      diff_l = (diff_l + static_cast<std::int32_t>(cur[1] & 255) - (last[1] & 255)) / 2;
      std::int32_t corr =
          static_cast<std::int32_t>(cur[2] & 255) - u8_clamp(diff_l + (last[2] & 255));
      enc.encodeSymbol(*ctxp->m_rgb_diff_4, u8_fold(corr));
    }
    if (sym & (1 << 3)) {
      std::int32_t corr =
          static_cast<std::int32_t>(cur[1] >> 8) - u8_clamp(diff_h + (last[1] >> 8));
      enc.encodeSymbol(*ctxp->m_rgb_diff_3, u8_fold(corr));
    }
    if (sym & (1 << 5)) {
      diff_h = (diff_h + static_cast<std::int32_t>(cur[1] >> 8) - (last[1] >> 8)) / 2;
      std::int32_t corr =
          static_cast<std::int32_t>(cur[2] >> 8) - u8_clamp(diff_h + (last[2] >> 8));
      enc.encodeSymbol(*ctxp->m_rgb_diff_5, u8_fold(corr));
    }
  }

  if (sym) changed_ = true;
  std::memcpy(ctxp->last_item, item, kRgb14Size);
}

void Rgb14CompressorV3::finish_chunk(std::uint32_t sizes[kLayers]) {
  if (!changed_) {
    sizes[0] = 0;
    return;
  }
  enc_[0].done();
  sizes[0] = layer_out_[0].size();
}

void Rgb14CompressorV3::write_layer_bytes(ByteStreamOut* out) {
  if (!changed_) return;
  out->put_bytes(layer_out_[0].data(), layer_out_[0].size());
}

void Rgb14DecompressorV3::create_and_init_models_and_decompressors(std::uint32_t context) {
  Rgb14Context* ctxp = &contexts_[context];
  if (!ctxp->created) {
    ctxp->m_byte_used = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_0 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_1 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_2 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_3 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_4 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_5 = std::make_unique<ArithmeticModel>();
    ctxp->created = true;
  }
  ctxp->m_byte_used->init(128, false);
  ctxp->m_rgb_diff_0->init(256, false);
  ctxp->m_rgb_diff_1->init(256, false);
  ctxp->m_rgb_diff_2->init(256, false);
  ctxp->m_rgb_diff_3->init(256, false);
  ctxp->m_rgb_diff_4->init(256, false);
  ctxp->m_rgb_diff_5->init(256, false);
}

void Rgb14DecompressorV3::init(const std::uint32_t* sizes,
                               const std::uint8_t* layer_bytes,
                               std::uint32_t layer_bytes_len,
                               const std::uint8_t* first_item,
                               std::uint32_t context) {
  (void)layer_bytes_len;
  current_context_ = context;
  layer_changed_ = sizes[0] != 0;
  if (layer_changed_) {
    layer_buf_[0].assign(layer_bytes, layer_bytes + sizes[0]);
    layer_in_[0].init(layer_buf_[0].data(), static_cast<std::uint32_t>(layer_buf_[0].size()));
    dec_[0].init(&layer_in_[0]);
  }
  create_and_init_models_and_decompressors(context);
  std::memcpy(contexts_[context].last_item, first_item, kRgb14Size);
}

void Rgb14DecompressorV3::read(std::uint8_t* item, std::uint32_t& context) {
  Rgb14Context* ctxp = &contexts_[current_context_];
  const std::uint8_t* last_item = ctxp->last_item;
  if (current_context_ != context) {
    current_context_ = context;
    ctxp = &contexts_[current_context_];
    if (!ctxp->created) {
      create_and_init_models_and_decompressors(current_context_);
      std::memcpy(ctxp->last_item, last_item, kRgb14Size);
    }
    last_item = ctxp->last_item;
  }

  if (!layer_changed_) {
    std::memcpy(item, last_item, kRgb14Size);
    return;
  }

  ArithmeticDecoder& dec = dec_[0];
  std::uint16_t* cur = reinterpret_cast<std::uint16_t*>(item);
  const std::uint16_t* last = reinterpret_cast<const std::uint16_t*>(last_item);

  std::uint32_t sym = dec.decodeSymbol(*ctxp->m_byte_used);

  std::uint16_t r = static_cast<std::uint16_t>(last[0] & 0x00FF);
  if (sym & (1 << 0)) {
    std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_0));
    r = static_cast<std::uint16_t>(u8_fold(corr + (last[0] & 255)));
  }
  if (sym & (1 << 1)) {
    std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_1));
    r |= static_cast<std::uint16_t>(u8_fold(corr + (last[0] >> 8))) << 8;
  } else {
    r |= static_cast<std::uint16_t>(last[0] & 0xFF00);
  }
  cur[0] = r;

  if (sym & (1 << 6)) {
    std::int32_t diff = (cur[0] & 0x00FF) - (last[0] & 0x00FF);
    std::uint16_t g = static_cast<std::uint16_t>(last[1] & 0x00FF);
    if (sym & (1 << 2)) {
      std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_2));
      g = static_cast<std::uint16_t>(u8_fold(corr + u8_clamp(diff + (last[1] & 255))));
    }
    std::uint16_t b = static_cast<std::uint16_t>(last[2] & 0x00FF);
    if (sym & (1 << 4)) {
      std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_4));
      diff = (diff + static_cast<std::int32_t>(g & 0x00FF) - (last[1] & 0x00FF)) / 2;
      b = static_cast<std::uint16_t>(u8_fold(corr + u8_clamp(diff + (last[2] & 255))));
    }
    cur[1] = g;
    diff = (cur[0] >> 8) - (last[0] >> 8);
    if (sym & (1 << 3)) {
      std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_3));
      cur[1] |= static_cast<std::uint16_t>(u8_fold(corr + u8_clamp(diff + (last[1] >> 8)))) << 8;
    } else {
      cur[1] |= static_cast<std::uint16_t>(last[1] & 0xFF00);
    }
    if (sym & (1 << 5)) {
      std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_5));
      diff = (diff + static_cast<std::int32_t>(cur[1] >> 8) - (last[1] >> 8)) / 2;
      b |= static_cast<std::uint16_t>(u8_fold(corr + u8_clamp(diff + (last[2] >> 8)))) << 8;
    } else {
      b |= static_cast<std::uint16_t>(last[2] & 0xFF00);
    }
    cur[2] = b;
  } else {
    cur[1] = cur[0];
    cur[2] = cur[0];
  }

  std::memcpy(ctxp->last_item, item, kRgb14Size);
}


void RgbNir14CompressorV3::create_and_init_models_and_compressors(std::uint32_t context) {
  RgbNir14Context* ctxp = &contexts_[context];
  if (!ctxp->created) {
    ctxp->m_rgb_bytes_used = std::make_unique<ArithmeticModel>();
    ctxp->m_nir_bytes_used = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_0 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_1 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_2 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_3 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_4 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_5 = std::make_unique<ArithmeticModel>();
    ctxp->m_nir_diff_0 = std::make_unique<ArithmeticModel>();
    ctxp->m_nir_diff_1 = std::make_unique<ArithmeticModel>();
    ctxp->created = true;
  }
  ctxp->m_rgb_bytes_used->init(256, true);
  ctxp->m_nir_bytes_used->init(256, true);
  ctxp->m_rgb_diff_0->init(256, true);
  ctxp->m_rgb_diff_1->init(256, true);
  ctxp->m_rgb_diff_2->init(256, true);
    ctxp->m_rgb_diff_3->init(256, true);
    ctxp->m_rgb_diff_4->init(256, true);
    ctxp->m_rgb_diff_5->init(256, true);
  ctxp->m_nir_diff_0->init(256, true);
  ctxp->m_nir_diff_1->init(256, true);
}

void RgbNir14CompressorV3::init(const std::uint8_t* first_item, std::uint32_t context) {
  current_context_ = context;
  for (int i = 0; i < kLayers; ++i) {
    layer_out_[i] = ByteStreamOut();
    enc_[i].init(&layer_out_[i]);
  }
  changed_[0] = false;
  changed_[1] = false;
  create_and_init_models_and_compressors(context);
  std::memcpy(contexts_[context].last_item, first_item, kRgbNir14Size);
}

void RgbNir14CompressorV3::write(const std::uint8_t* item, std::uint32_t context) {
  RgbNir14Context* ctxp = &contexts_[current_context_];
  const std::uint8_t* last_item = ctxp->last_item;
  if (current_context_ != context) {
    current_context_ = context;
    ctxp = &contexts_[current_context_];
    if (!ctxp->created) {
      create_and_init_models_and_compressors(current_context_);
      std::memcpy(ctxp->last_item, last_item, kRgbNir14Size);
    }
    last_item = ctxp->last_item;
  }

  const std::uint16_t* cur = reinterpret_cast<const std::uint16_t*>(item);
  const std::uint16_t* last = reinterpret_cast<const std::uint16_t*>(last_item);
  std::uint32_t sym = rgb_sym(cur, last);
  enc_[0].encodeSymbol(*ctxp->m_rgb_bytes_used, sym);

  std::int32_t diff_l = 0;
  std::int32_t diff_h = 0;
  if (sym & (1 << 0)) {
    diff_l = static_cast<std::int32_t>(cur[0] & 255) - (last[0] & 255);
    enc_[0].encodeSymbol(*ctxp->m_rgb_diff_0, u8_fold(diff_l));
  }
  if (sym & (1 << 1)) {
    diff_h = static_cast<std::int32_t>(cur[0] >> 8) - (last[0] >> 8);
    enc_[0].encodeSymbol(*ctxp->m_rgb_diff_1, u8_fold(diff_h));
  }
  if (sym & (1 << 6)) {
    if (sym & (1 << 2)) {
      std::int32_t corr =
          static_cast<std::int32_t>(cur[1] & 255) - u8_clamp(diff_l + (last[1] & 255));
      enc_[0].encodeSymbol(*ctxp->m_rgb_diff_2, u8_fold(corr));
    }
    if (sym & (1 << 4)) {
      diff_l = (diff_l + static_cast<std::int32_t>(cur[1] & 255) - (last[1] & 255)) / 2;
      std::int32_t corr =
          static_cast<std::int32_t>(cur[2] & 255) - u8_clamp(diff_l + (last[2] & 255));
      enc_[0].encodeSymbol(*ctxp->m_rgb_diff_4, u8_fold(corr));
    }
    if (sym & (1 << 3)) {
      std::int32_t corr =
          static_cast<std::int32_t>(cur[1] >> 8) - u8_clamp(diff_h + (last[1] >> 8));
      enc_[0].encodeSymbol(*ctxp->m_rgb_diff_3, u8_fold(corr));
    }
    if (sym & (1 << 5)) {
      diff_h = (diff_h + static_cast<std::int32_t>(cur[1] >> 8) - (last[1] >> 8)) / 2;
      std::int32_t corr =
          static_cast<std::int32_t>(cur[2] >> 8) - u8_clamp(diff_h + (last[2] >> 8));
      enc_[0].encodeSymbol(*ctxp->m_rgb_diff_5, u8_fold(corr));
    }
  }
  if (sym) changed_[0] = true;

  sym = static_cast<std::uint32_t>((last[3] & 0x00FF) != (cur[3] & 0x00FF));
  sym |= static_cast<std::uint32_t>((last[3] & 0xFF00) != (cur[3] & 0xFF00)) << 1;
  enc_[1].encodeSymbol(*ctxp->m_nir_bytes_used, sym);
  if (sym & (1 << 0)) {
    std::int32_t diff = static_cast<std::int32_t>(cur[3] & 255) - (last[3] & 255);
    enc_[1].encodeSymbol(*ctxp->m_nir_diff_0, u8_fold(diff));
  }
  if (sym & (1 << 1)) {
    std::int32_t diff = static_cast<std::int32_t>(cur[3] >> 8) - (last[3] >> 8);
    enc_[1].encodeSymbol(*ctxp->m_nir_diff_1, u8_fold(diff));
  }
  if (sym) changed_[1] = true;

  std::memcpy(ctxp->last_item, item, kRgbNir14Size);
}

void RgbNir14CompressorV3::finish_chunk(std::uint32_t sizes[kLayers]) {
  for (int i = 0; i < kLayers; ++i) {
    if (changed_[i]) {
      enc_[i].done();
      sizes[i] = layer_out_[i].size();
    } else {
      sizes[i] = 0;
    }
  }
}

void RgbNir14CompressorV3::write_layer_bytes(ByteStreamOut* out) {
  for (int i = 0; i < kLayers; ++i) {
    if (!changed_[i]) continue;
    out->put_bytes(layer_out_[i].data(), layer_out_[i].size());
  }
}

void RgbNir14DecompressorV3::create_and_init_models_and_decompressors(std::uint32_t context) {
  RgbNir14Context* ctxp = &contexts_[context];
  if (!ctxp->created) {
    ctxp->m_rgb_bytes_used = std::make_unique<ArithmeticModel>();
    ctxp->m_nir_bytes_used = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_0 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_1 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_2 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_3 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_4 = std::make_unique<ArithmeticModel>();
    ctxp->m_rgb_diff_5 = std::make_unique<ArithmeticModel>();
    ctxp->m_nir_diff_0 = std::make_unique<ArithmeticModel>();
    ctxp->m_nir_diff_1 = std::make_unique<ArithmeticModel>();
    ctxp->created = true;
  }
  ctxp->m_rgb_bytes_used->init(256, false);
  ctxp->m_nir_bytes_used->init(256, false);
  ctxp->m_rgb_diff_0->init(256, false);
  ctxp->m_rgb_diff_1->init(256, false);
  ctxp->m_rgb_diff_2->init(256, false);
  ctxp->m_rgb_diff_3->init(256, false);
  ctxp->m_rgb_diff_4->init(256, false);
  ctxp->m_rgb_diff_5->init(256, false);
  ctxp->m_nir_diff_0->init(256, false);
  ctxp->m_nir_diff_1->init(256, false);
}

void RgbNir14DecompressorV3::init(const std::uint32_t* sizes,
                                   const std::uint8_t* layer_bytes,
                                   std::uint32_t layer_bytes_len,
                                   const std::uint8_t* first_item,
                                   std::uint32_t context) {
  (void)layer_bytes_len;
  current_context_ = context;
  std::uint32_t off = 0;
  for (int i = 0; i < kLayers; ++i) {
    layer_changed_[i] = sizes[i] != 0;
    if (sizes[i]) {
      layer_buf_[i].assign(layer_bytes + off, layer_bytes + off + sizes[i]);
      off += sizes[i];
      layer_in_[i].init(layer_buf_[i].data(), static_cast<std::uint32_t>(layer_buf_[i].size()));
      dec_[i].init(&layer_in_[i]);
    } else {
      off += 0;
    }
  }
  create_and_init_models_and_decompressors(context);
  std::memcpy(contexts_[context].last_item, first_item, kRgbNir14Size);
}

void RgbNir14DecompressorV3::read(std::uint8_t* item, std::uint32_t& context) {
  RgbNir14Context* ctxp = &contexts_[current_context_];
  const std::uint8_t* last_item = ctxp->last_item;
  if (current_context_ != context) {
    current_context_ = context;
    ctxp = &contexts_[current_context_];
    if (!ctxp->created) {
      create_and_init_models_and_decompressors(current_context_);
      std::memcpy(ctxp->last_item, last_item, kRgbNir14Size);
    }
    last_item = ctxp->last_item;
  }

  std::uint16_t* cur = reinterpret_cast<std::uint16_t*>(item);
  const std::uint16_t* last = reinterpret_cast<const std::uint16_t*>(last_item);
  std::memcpy(item, last_item, kRgbNir14Size);

  if (layer_changed_[0]) {
    ArithmeticDecoder& dec = dec_[0];
    std::uint32_t sym = dec.decodeSymbol(*ctxp->m_rgb_bytes_used);
    if (sym & (1 << 0)) {
      std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_0));
      cur[0] = static_cast<std::uint16_t>(u8_fold(corr + (last[0] & 255)));
    } else {
      cur[0] = static_cast<std::uint16_t>(last[0] & 0x00FF);
    }
    if (sym & (1 << 1)) {
      std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_1));
      cur[0] |= static_cast<std::uint16_t>(u8_fold(corr + (last[0] >> 8))) << 8;
    } else {
      cur[0] |= static_cast<std::uint16_t>(last[0] & 0xFF00);
    }
    if (sym & (1 << 6)) {
      std::int32_t diff = (cur[0] & 0x00FF) - (last[0] & 0x00FF);
      if (sym & (1 << 2)) {
        std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_2));
        cur[1] = static_cast<std::uint16_t>(u8_fold(corr + u8_clamp(diff + (last[1] & 255))));
      } else {
        cur[1] = static_cast<std::uint16_t>(last[1] & 0x00FF);
      }
      if (sym & (1 << 4)) {
        std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_4));
        diff = (diff + static_cast<std::int32_t>(cur[1] & 0x00FF) - (last[1] & 0x00FF)) / 2;
        cur[2] = static_cast<std::uint16_t>(u8_fold(corr + u8_clamp(diff + (last[2] & 255))));
      } else {
        cur[2] = static_cast<std::uint16_t>(last[2] & 0x00FF);
      }
      diff = (cur[0] >> 8) - (last[0] >> 8);
      if (sym & (1 << 3)) {
        std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_3));
        cur[1] |= static_cast<std::uint16_t>(u8_fold(corr + u8_clamp(diff + (last[1] >> 8)))) << 8;
      } else {
        cur[1] |= static_cast<std::uint16_t>(last[1] & 0xFF00);
      }
      if (sym & (1 << 5)) {
        std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_rgb_diff_5));
        diff = (diff + static_cast<std::int32_t>(cur[1] >> 8) - (last[1] >> 8)) / 2;
        cur[2] |= static_cast<std::uint16_t>(u8_fold(corr + u8_clamp(diff + (last[2] >> 8)))) << 8;
      } else {
        cur[2] |= static_cast<std::uint16_t>(last[2] & 0xFF00);
      }
    } else {
      cur[1] = cur[0];
      cur[2] = cur[0];
    }
  }

  if (layer_changed_[1]) {
    ArithmeticDecoder& dec = dec_[1];
    std::uint32_t sym = dec.decodeSymbol(*ctxp->m_nir_bytes_used);
    if (sym & (1 << 0)) {
      std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_nir_diff_0));
      cur[3] = static_cast<std::uint16_t>(u8_fold(corr + (last[3] & 255)));
    } else {
      cur[3] = static_cast<std::uint16_t>(last[3] & 0x00FF);
    }
    if (sym & (1 << 1)) {
      std::int32_t corr = static_cast<std::int32_t>(dec.decodeSymbol(*ctxp->m_nir_diff_1));
      cur[3] |= static_cast<std::uint16_t>(u8_fold(corr + (last[3] >> 8))) << 8;
    } else {
      cur[3] |= static_cast<std::uint16_t>(last[3] & 0xFF00);
    }
  }

  std::memcpy(ctxp->last_item, item, kRgbNir14Size);
}

}
