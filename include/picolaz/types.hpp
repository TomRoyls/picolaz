#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <span>
#include <string>

namespace picolaz {

// ─── Fundamental types ───────────────────────────────────────────────
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

// ─── Union types for type-punning (matching LASzip exactly) ─────────
union U32I32F32 { u32 u; i32 i; f32 f; };
union U64I64F64 { u64 u; i64 i; f64 f; };

// ─── Helpers ─────────────────────────────────────────────────────────
inline constexpr u32 u32_zero_bit_0(u32 n) { return n & 0xFFFFFFFEu; }

inline constexpr i32 i32_quantize(f32 n) {
    return n >= 0 ? static_cast<i32>(n + 0.5f) : static_cast<i32>(n - 0.5f);
}

inline constexpr i8 i8_clamp(i32 n) {
    return n <= -128 ? -128 : (n >= 127 ? 127 : static_cast<i8>(n));
}

// ─── LAS 1.4 public header (375 bytes) ─────────────────────────────
#pragma pack(push, 1)
struct LasHeader {
    char     file_signature[4]{'L','A','S','F'};
    u16      file_source_id{0};
    u16      global_encoding{0};      // bit 4 set = WKT
    u32      guid_1{0};
    u16      guid_2{0};
    u16      guid_3{0};
    u8       guid_4[8]{};
    u8       version_major{1};
    u8       version_minor{4};
    char     system_identifier[32]{};
    char     generating_software[32]{};
    u16      file_creation_day{0};
    u16      file_creation_year{0};
    u16      header_size{375};
    u32      offset_to_point_data{0};
    u32      number_of_vlrs{0};
    u8       point_data_record_format{6};
    u16      point_data_record_length{30};
    u32      legacy_number_of_point_records{0};
    u32      legacy_number_of_points_by_return[5]{};
    f64      x_scale_factor{0.001};
    f64      y_scale_factor{0.001};
    f64      z_scale_factor{0.001};
    f64      x_offset{0.0};
    f64      y_offset{0.0};
    f64      z_offset{0.0};
    f64      max_x{0.0};
    f64      min_x{0.0};
    f64      max_y{0.0};
    f64      min_y{0.0};
    f64      max_z{0.0};
    f64      min_z{0.0};
    u64      start_of_waveform_data_packet_record{0};
    u64      start_of_first_evlr{0};
    u32      number_of_evlrs{0};
    u64      number_of_point_records{0};
    u64      number_of_points_by_return[15]{};
};
static_assert(sizeof(LasHeader) == 375);

// ─── VLR header (54 bytes) ──────────────────────────────────────────
struct VlrHeader {
    u16  reserved{0};
    char user_id[16]{};
    u16  record_id{0};
    u16  record_length_after_header{0};
    char description[32]{};
};
static_assert(sizeof(VlrHeader) == 54);

// ─── EVLR header (60 bytes) ─────────────────────────────────────────
struct EvlrHeader {
    u16  reserved{0};
    char user_id[16]{};
    u16  record_id{0};
    u64  record_length_after_header{0};
    char description[32]{};
};
static_assert(sizeof(EvlrHeader) == 60);
#pragma pack(pop)

// ─── LASzip VLR item record ─────────────────────────────────────────
struct LaszipItemRecord {
    u16 type;       // 6=POINT14, 7=RGB14, 8=RGBNIR14, 9=BYTE14
    u16 size;
    u16 version;    // 4 for native v4 compressor
};

// ─── LASzip VLR payload ─────────────────────────────────────────────
struct LaszipVlr {
    u16 compressor{3};          // 3 = POINTWISE_CHUNKED (native LAZ 1.4)
    u16 coder{0};               // 0 = arithmetic
    u8  version_major{3};
    u8  version_minor{4};
    u16 version_revision{0};
    u32 options{0};             // chunk size
    u32 chunk_size{50000};
    i64 num_special_evlrs{-1};
    i64 offset_special_evlrs{-1};
    u16 num_items{0};
    std::vector<LaszipItemRecord> items;
};

// ─── Point types ────────────────────────────────────────────────────

// Internal representation matching LASzip's LASpoint14 memory layout
// for compression compatibility. All point types 6-8 use this base.
struct Point14 {
    i32 x{0};
    i32 y{0};
    i32 z{0};
    u16 intensity{0};
    u8  return_number{0};         // 4 bits (0-15)
    u8  number_of_returns{0};     // 4 bits (0-15)
    u8  classification_flags{0};  // 4 bits
    u8  scanner_channel{0};       // 2 bits
    u8  scan_direction_flag{0};   // 1 bit
    u8  edge_of_flight_line{0};   // 1 bit
    u8  classification{0};
    u8  user_data{0};
    i16 scan_angle{0};            // i16 in point type 6+ (0.006 degree units)
    u16 point_source_id{0};
    f64 gps_time{0.0};

    // RGB (for point types 7, 8)
    u16 red{0};
    u16 green{0};
    u16 blue{0};

    // NIR (for point type 8)
    u16 nir{0};
};

// ─── Point format enum ──────────────────────────────────────────────
enum class PointFormat : u8 {
    Format6 = 6,
    Format7 = 7,
    Format8 = 8,
};

inline u16 point_record_length(PointFormat fmt) {
    switch (fmt) {
        case PointFormat::Format6: return 30;
        case PointFormat::Format7: return 36;
        case PointFormat::Format8: return 38;
    }
    throw std::runtime_error("unsupported point format");
}

inline bool format_has_rgb(PointFormat fmt) {
    return fmt == PointFormat::Format7 || fmt == PointFormat::Format8;
}

inline bool format_has_nir(PointFormat fmt) {
    return fmt == PointFormat::Format8;
}

// ─── Exceptions ─────────────────────────────────────────────────────
class LazError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace picolaz
