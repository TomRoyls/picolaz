#pragma once
// Bit-exact implementation of the LAZ 1.4 layered-chunked item coding used by
// LASzip/LAZperf: items POINT14 (type 10) version 3, RGB14 (type 11) version 3,
// and RGBNIR14 (type 12) version 3, compressor 3, plus the standard chunk
// container ([raw first point][u32 count][u32 per-layer sizes][layer bytes],
// chunk table referenced by a u64 offset at the start of the point data and
// itself delta-coded with an integer compressor).
//
// The algorithms follow the LASzip implementation (rapidlasso GmbH,
// Apache-2.0) closely to guarantee byte-level interoperability with LASzip
// and LAZperf; this file is an independent MPL-2.0 adaptation for picolaz.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "picolaz/arithmetic_codec.hpp"
#include "picolaz/compressor_types.hpp"
#include "picolaz/integer_compressor.hpp"
#include "picolaz/streams.hpp"

namespace picolaz::v3 {

inline constexpr std::uint32_t kItemPoint14 = 10;
inline constexpr std::uint32_t kItemRgb14 = 11;
inline constexpr std::uint32_t kItemRgbNir14 = 12;

inline constexpr int kPoint14Size = 30;
inline constexpr int kRgb14Size = 6;
inline constexpr int kRgbNir14Size = 8;
inline constexpr int kMaxContexts = 4;  // LAS 1.4 scanner channels

inline constexpr std::int32_t kGpsTimeMulti = 500;
inline constexpr std::int32_t kGpsTimeMultiMinus = -10;
inline constexpr std::int32_t kGpsTimeMultiCodeFull =
    kGpsTimeMulti - kGpsTimeMultiMinus + 1;
inline constexpr std::int32_t kGpsTimeMultiTotal =
    kGpsTimeMulti - kGpsTimeMultiMinus + 5;

// Layer order within a chunk, matching LASzip's POINT14 v3 item.
inline constexpr int kPoint14Layers = 9;
enum Point14Layer {
  LAYER_CHANNEL_RETURNS_XY = 0,
  LAYER_Z = 1,
  LAYER_CLASSIFICATION = 2,
  LAYER_FLAGS = 3,
  LAYER_INTENSITY = 4,
  LAYER_SCAN_ANGLE = 5,
  LAYER_USER_DATA = 6,
  LAYER_POINT_SOURCE = 7,
  LAYER_GPS_TIME = 8,
};

std::int32_t quantize_i32(double v);

struct Point14Context {
  bool created = false;
  bool unused = true;

  std::unique_ptr<ArithmeticModel> m_changed_values[8];   // 128 symbols each
  std::unique_ptr<ArithmeticModel> m_scanner_channel;     // 3 symbols
  std::unique_ptr<ArithmeticModel> m_number_of_returns[16];  // 16, lazy
  std::unique_ptr<ArithmeticModel> m_return_number[16];      // 16, lazy
  std::unique_ptr<ArithmeticModel> m_return_number_gps_same;  // 13
  std::unique_ptr<ArithmeticModel> m_classification[64];      // 256, lazy
  std::unique_ptr<ArithmeticModel> m_flags[64];               // 64, lazy
  std::unique_ptr<ArithmeticModel> m_user_data[64];           // 256, lazy
  std::unique_ptr<ArithmeticModel> m_gpstime_multi;   // kGpsTimeMultiTotal
  std::unique_ptr<ArithmeticModel> m_gpstime_0diff;   // 5 symbols

  std::unique_ptr<IntegerCompressor> ic_dX;    // (32, 2)
  std::unique_ptr<IntegerCompressor> ic_dY;    // (32, 22)
  std::unique_ptr<IntegerCompressor> ic_Z;     // (32, 20)
  std::unique_ptr<IntegerCompressor> ic_intensity;      // (16, 4)
  std::unique_ptr<IntegerCompressor> ic_scan_angle;     // (16, 2)
  std::unique_ptr<IntegerCompressor> ic_point_source;   // (16, 1)
  std::unique_ptr<IntegerCompressor> ic_gpstime;        // (32, 9)

  StreamingMedian5 last_X_diff_median5[12];  // index (m << 1) | gps_time_change
  StreamingMedian5 last_Y_diff_median5[12];
  std::int32_t last_Z[8] = {};
  std::uint16_t last_intensity[8] = {};

  std::uint8_t last_item[kPoint14Size] = {};
  bool last_gps_time_change = false;

  // gps time state: four interleaved pulse sequences (slots 0-3)
  std::int64_t last_gpstime[4] = {};
  std::int32_t last_gpstime_diff[4] = {};
  std::int32_t multi_extreme_counter[4] = {};
  std::uint32_t gps_last = 0;
  std::uint32_t gps_next = 0;
};

class Point14CompressorV3 {
 public:
  void init(const std::uint8_t* first_item, std::uint32_t context);
  void write(const std::uint8_t* item, std::uint32_t context);
  // Flush changed layer encoders and report the 9 layer sizes (0 for layers
  // that never changed; the XY and Z layers always count as changed).
  void finish_chunk(std::uint32_t sizes[kPoint14Layers]);
  // Append the bytes of every changed layer, in layer order.
  void write_layer_bytes(ByteStreamOut* out);

 private:
  void create_and_init_models_and_compressors(std::uint32_t context,
                                               const std::uint8_t* item);
  void write_gps_time(std::int64_t gps_time);

  std::array<ByteStreamOut, kPoint14Layers> layer_out_;
  std::array<ArithmeticEncoder, kPoint14Layers> enc_;
  std::array<bool, kPoint14Layers> changed_{};
  Point14Context contexts_[kMaxContexts];
  std::uint32_t current_context_ = 0;
};

class Point14DecompressorV3 {
 public:
  // sizes: the 9 layer sizes for this chunk; layer_bytes: the layer byte
  // region (layers stored back to back in layer order).
  void init(const std::uint32_t* sizes, const std::uint8_t* layer_bytes,
            std::uint32_t layer_bytes_len, const std::uint8_t* first_item,
            std::uint32_t context);
  void read(std::uint8_t* item, std::uint32_t& context);

 private:
  void create_and_init_models_and_decompressors(std::uint32_t context,
                                                const std::uint8_t* item);
  std::int64_t read_gps_time();

  std::array<std::vector<std::uint8_t>, kPoint14Layers> layer_buf_;
  std::array<ByteStreamIn, kPoint14Layers> layer_in_;
  std::array<ArithmeticDecoder, kPoint14Layers> dec_;
  std::array<bool, kPoint14Layers> layer_avail_{};
  Point14Context contexts_[kMaxContexts];
  std::uint32_t current_context_ = 0;
};

struct Rgb14Context {
  bool created = false;
  std::unique_ptr<ArithmeticModel> m_byte_used;   // 128 symbols
  std::unique_ptr<ArithmeticModel> m_rgb_diff_0;  // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_1;  // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_2;  // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_3;  // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_4;  // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_5;  // 256
  std::uint8_t last_item[kRgb14Size] = {};
};

class Rgb14CompressorV3 {
 public:
  static constexpr int kLayers = 1;
  void init(const std::uint8_t* first_item, std::uint32_t context);
  void write(const std::uint8_t* item, std::uint32_t context);
  void finish_chunk(std::uint32_t sizes[kLayers]);
  void write_layer_bytes(ByteStreamOut* out);

 private:
  void create_and_init_models_and_compressors(std::uint32_t context);

  std::array<ByteStreamOut, kLayers> layer_out_;
  std::array<ArithmeticEncoder, kLayers> enc_;
  bool changed_ = false;
  Rgb14Context contexts_[kMaxContexts];
  std::uint32_t current_context_ = 0;
};

class Rgb14DecompressorV3 {
 public:
  static constexpr int kLayers = 1;
  void init(const std::uint32_t* sizes, const std::uint8_t* layer_bytes,
            std::uint32_t layer_bytes_len, const std::uint8_t* first_item,
            std::uint32_t context);
  void read(std::uint8_t* item, std::uint32_t& context);

 private:
  void create_and_init_models_and_decompressors(std::uint32_t context);


  std::array<std::vector<std::uint8_t>, kLayers> layer_buf_;
  std::array<ByteStreamIn, kLayers> layer_in_;
  std::array<ArithmeticDecoder, kLayers> dec_;
  bool layer_changed_ = false;
  Rgb14Context contexts_[kMaxContexts];
  std::uint32_t current_context_ = 0;
};

struct RgbNir14Context {
  bool created = false;
  std::unique_ptr<ArithmeticModel> m_rgb_bytes_used;  // 256
  std::unique_ptr<ArithmeticModel> m_nir_bytes_used;  // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_0;      // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_1;      // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_2;      // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_3;      // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_4;      // 256
  std::unique_ptr<ArithmeticModel> m_rgb_diff_5;      // 256
  std::unique_ptr<ArithmeticModel> m_nir_diff_0;      // 256
  std::unique_ptr<ArithmeticModel> m_nir_diff_1;      // 256
  std::uint8_t last_item[kRgbNir14Size] = {};
};

class RgbNir14CompressorV3 {
 public:
  static constexpr int kLayers = 2;  // RGB layer + NIR layer
  void init(const std::uint8_t* first_item, std::uint32_t context);
  void write(const std::uint8_t* item, std::uint32_t context);
  void finish_chunk(std::uint32_t sizes[kLayers]);
  void write_layer_bytes(ByteStreamOut* out);

 private:
  void create_and_init_models_and_compressors(std::uint32_t context);

  std::array<ByteStreamOut, kLayers> layer_out_;
  std::array<ArithmeticEncoder, kLayers> enc_;
  std::array<bool, kLayers> changed_{};
  RgbNir14Context contexts_[kMaxContexts];
  std::uint32_t current_context_ = 0;
};

class RgbNir14DecompressorV3 {
 public:
  static constexpr int kLayers = 2;
  void init(const std::uint32_t* sizes, const std::uint8_t* layer_bytes,
            std::uint32_t layer_bytes_len, const std::uint8_t* first_item,
            std::uint32_t context);
  void read(std::uint8_t* item, std::uint32_t& context);

 private:
  void create_and_init_models_and_decompressors(std::uint32_t context);

  std::array<std::vector<std::uint8_t>, kLayers> layer_buf_;
  std::array<ByteStreamIn, kLayers> layer_in_;
  std::array<ArithmeticDecoder, kLayers> dec_;
  std::array<bool, kLayers> layer_changed_{};
  RgbNir14Context contexts_[kMaxContexts];
  std::uint32_t current_context_ = 0;
};

}
