#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <span>
#include <string>

namespace picolaz {

union U32I32F32 { std::uint32_t u; std::int32_t i; float f; };
union U64I64F64 { std::uint64_t u; std::int64_t i; double f; };

inline constexpr std::uint32_t u32_zero_bit_0(std::uint32_t n) { return n & 0xFFFFFFFEu; }

inline constexpr std::int32_t i32_quantize(float n) {
    return n >= 0 ? static_cast<std::int32_t>(n + 0.5f) : static_cast<std::int32_t>(n - 0.5f);
}

inline constexpr std::int8_t i8_clamp(std::int32_t n) {
    return n <= -128 ? -128 : (n >= 127 ? 127 : static_cast<std::int8_t>(n));
}

// ─── LAS 1.4 public header (375 bytes) ─────────────────────────────
#pragma pack(push, 1)
struct LasHeader {
    char     file_signature[4]{'L','A','S','F'};
    std::uint16_t      file_source_id{0};
    std::uint16_t      global_encoding{0};      // bit 4 set = WKT
    std::uint32_t      guid_1{0};
    std::uint16_t      guid_2{0};
    std::uint16_t      guid_3{0};
    std::uint8_t       guid_4[8]{};
    std::uint8_t       version_major{1};
    std::uint8_t       version_minor{4};
    char     system_identifier[32]{};
    char     generating_software[32]{};
    std::uint16_t      file_creation_day{0};
    std::uint16_t      file_creation_year{0};
    std::uint16_t      header_size{375};
    std::uint32_t      offset_to_point_data{0};
    std::uint32_t      number_of_vlrs{0};
    std::uint8_t       point_data_record_format{6};
    std::uint16_t      point_data_record_length{30};
    std::uint32_t      legacy_number_of_point_records{0};
    std::uint32_t      legacy_number_of_points_by_return[5]{};
    double      x_scale_factor{0.001};
    double      y_scale_factor{0.001};
    double      z_scale_factor{0.001};
    double      x_offset{0.0};
    double      y_offset{0.0};
    double      z_offset{0.0};
    double      max_x{0.0};
    double      min_x{0.0};
    double      max_y{0.0};
    double      min_y{0.0};
    double      max_z{0.0};
    double      min_z{0.0};
    std::uint64_t      start_of_waveform_data_packet_record{0};
    std::uint64_t      start_of_first_evlr{0};
    std::uint32_t      number_of_evlrs{0};
    std::uint64_t      number_of_point_records{0};
    std::uint64_t      number_of_points_by_return[15]{};
};
static_assert(sizeof(LasHeader) == 375);

// ─── VLR header (54 bytes) ──────────────────────────────────────────
struct VlrHeader {
    std::uint16_t  reserved{0};
    char user_id[16]{};
    std::uint16_t  record_id{0};
    std::uint16_t  record_length_after_header{0};
    char description[32]{};
};
static_assert(sizeof(VlrHeader) == 54);

// ─── EVLR header (60 bytes) ─────────────────────────────────────────
struct EvlrHeader {
    std::uint16_t  reserved{0};
    char user_id[16]{};
    std::uint16_t  record_id{0};
    std::uint64_t  record_length_after_header{0};
    char description[32]{};
};
static_assert(sizeof(EvlrHeader) == 60);
#pragma pack(pop)

// ─── LASzip VLR item record ─────────────────────────────────────────
struct LaszipItemRecord {
    std::uint16_t type;       // 6=POINT14, 7=RGB14, 8=RGBNIR14, 9=BYTE14
    std::uint16_t size;
    std::uint16_t version;    // 4 for native v4 compressor
};

// ─── LASzip VLR payload ─────────────────────────────────────────────
struct LaszipVlr {
    std::uint16_t compressor{3};          // 3 = POINTWISE_CHUNKED (native LAZ 1.4)
    std::uint16_t coder{0};               // 0 = arithmetic
    std::uint8_t  version_major{3};
    std::uint8_t  version_minor{4};
    std::uint16_t version_revision{0};
    std::uint32_t options{0};             // chunk size
    std::uint32_t chunk_size{50000};
    std::int64_t num_special_evlrs{-1};
    std::int64_t offset_special_evlrs{-1};
    std::uint16_t num_items{0};
    std::vector<LaszipItemRecord> items;
};

// ─── Point types ────────────────────────────────────────────────────

// Internal representation matching LASzip's LASpoint14 memory layout
// for compression compatibility. All point types 6-8 use this base.
struct Point14 {
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t z{0};
    std::uint16_t intensity{0};
    std::uint8_t  return_number{0};         // 4 bits (0-15)
    std::uint8_t  number_of_returns{0};     // 4 bits (0-15)
    std::uint8_t  classification_flags{0};  // 4 bits
    std::uint8_t  scanner_channel{0};       // 2 bits
    std::uint8_t  scan_direction_flag{0};   // 1 bit
    std::uint8_t  edge_of_flight_line{0};   // 1 bit
    std::uint8_t  classification{0};
    std::uint8_t  user_data{0};
    std::int16_t scan_angle{0};            // std::int16_t in point type 6+ (0.006 degree units)
    std::uint16_t point_source_id{0};
    double gps_time{0.0};

    // RGB (for point types 7, 8)
    std::uint16_t red{0};
    std::uint16_t green{0};
    std::uint16_t blue{0};

    // NIR (for point type 8)
    std::uint16_t nir{0};
};

// ─── Point format enum ──────────────────────────────────────────────
enum class PointFormat : std::uint8_t {
    Format6 = 6,
    Format7 = 7,
    Format8 = 8,
};

inline std::uint16_t point_record_length(PointFormat fmt) {
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
