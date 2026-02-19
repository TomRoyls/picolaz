#pragma once

#include "types.hpp"
#include "arithmetic_codec.hpp"
#include "integer_compressor.hpp"
#include <memory>
#include <cstring>

namespace picolaz {

struct StreamingMedian5 {
    std::int32_t values[5]{};
    bool high{true};

    void init() {
        std::memset(values, 0, sizeof(values));
        high = true;
    }

    void add(std::int32_t v) {
        if (high) {
            if (v < values[2]) {
                values[4] = values[3];
                values[3] = values[2];
                if (v < values[0]) {
                    values[2] = values[1];
                    values[1] = values[0];
                    values[0] = v;
                } else if (v < values[1]) {
                    values[2] = values[1];
                    values[1] = v;
                } else {
                    values[2] = v;
                }
            } else {
                if (v < values[3]) {
                    values[4] = values[3];
                    values[3] = v;
                } else {
                    values[4] = v;
                }
                high = false;
            }
        } else {
            if (v > values[2]) {
                values[0] = values[1];
                values[1] = values[2];
                if (v > values[4]) {
                    values[2] = values[3];
                    values[3] = values[4];
                    values[4] = v;
                } else if (v > values[3]) {
                    values[2] = values[3];
                    values[3] = v;
                } else {
                    values[2] = v;
                }
            } else {
                if (v > values[1]) {
                    values[0] = values[1];
                    values[1] = v;
                } else {
                    values[0] = v;
                }
                high = true;
            }
        }
    }

    std::int32_t get() const { return values[2]; }
};

// Maps (number_of_returns, return_number) to 6 contexts
// Copied from LASzip's laszip_common_v3.hpp for bit-compatibility
inline constexpr std::uint8_t number_return_map_6ctx[16][16] = {
    {  0,  1,  2,  3,  4,  5,  3,  4,  4,  5,  5,  5,  5,  5,  5,  5 },
    {  1,  0,  1,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3 },
    {  2,  1,  2,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4 },
    {  3,  3,  4,  0,  1,  2,  3,  4,  4,  5,  5,  5,  5,  5,  5,  5 },
    {  4,  3,  4,  1,  0,  1,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3 },
    {  5,  3,  4,  2,  1,  2,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4 },
    {  3,  3,  4,  3,  3,  4,  0,  1,  2,  3,  4,  4,  5,  5,  5,  5 },
    {  4,  3,  4,  4,  3,  4,  1,  0,  1,  3,  3,  3,  3,  3,  3,  3 },
    {  4,  3,  4,  4,  3,  4,  2,  1,  2,  4,  4,  4,  4,  4,  4,  4 },
    {  5,  3,  4,  5,  3,  4,  3,  3,  4,  0,  1,  2,  3,  4,  4,  5 },
    {  5,  3,  4,  5,  3,  4,  4,  3,  4,  1,  0,  1,  3,  3,  3,  3 },
    {  5,  3,  4,  5,  3,  4,  5,  3,  4,  2,  1,  2,  4,  4,  4,  4 },
    {  5,  3,  4,  5,  3,  4,  5,  3,  4,  3,  3,  4,  0,  1,  2,  3 },
    {  5,  3,  4,  5,  3,  4,  5,  3,  4,  4,  3,  4,  1,  0,  1,  3 },
    {  5,  3,  4,  5,  3,  4,  5,  3,  4,  5,  3,  4,  2,  1,  2,  4 },
    {  5,  3,  4,  5,  3,  4,  5,  3,  4,  5,  3,  4,  3,  3,  4,  0 },
};

// Maps (number_of_returns, return_number) to 8 penetration level contexts
inline constexpr std::uint8_t number_return_level_8ctx[16][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7 },
    {  1,  0,  1,  2,  3,  4,  5,  6,  7,  7,  7,  7,  7,  7,  7,  7 },
    {  2,  1,  0,  1,  2,  3,  4,  5,  6,  7,  7,  7,  7,  7,  7,  7 },
    {  3,  2,  1,  0,  1,  2,  3,  4,  5,  6,  7,  7,  7,  7,  7,  7 },
    {  4,  3,  2,  1,  0,  1,  2,  3,  4,  5,  6,  7,  7,  7,  7,  7 },
    {  5,  4,  3,  2,  1,  0,  1,  2,  3,  4,  5,  6,  7,  7,  7,  7 },
    {  6,  5,  4,  3,  2,  1,  0,  1,  2,  3,  4,  5,  6,  7,  7,  7 },
    {  7,  6,  5,  4,  3,  2,  1,  0,  1,  2,  3,  4,  5,  6,  7,  7 },
    {  7,  7,  6,  5,  4,  3,  2,  1,  0,  1,  2,  3,  4,  5,  6,  7 },
    {  7,  7,  7,  6,  5,  4,  3,  2,  1,  0,  1,  2,  3,  4,  5,  6 },
    {  7,  7,  7,  7,  6,  5,  4,  3,  2,  1,  0,  1,  2,  3,  4,  5 },
    {  7,  7,  7,  7,  7,  6,  5,  4,  3,  2,  1,  0,  1,  2,  3,  4 },
    {  7,  7,  7,  7,  7,  7,  6,  5,  4,  3,  2,  1,  0,  1,  2,  3 },
    {  7,  7,  7,  7,  7,  7,  7,  6,  5,  4,  3,  2,  1,  0,  1,  2 },
    {  7,  7,  7,  7,  7,  7,  7,  7,  6,  5,  4,  3,  2,  1,  0,  1 },
    {  7,  7,  7,  7,  7,  7,  7,  7,  7,  6,  5,  4,  3,  2,  1,  0 },
};

// Internal packed point layout matching LASzip's LASpoint14 for compression
#pragma pack(push, 1)
struct PackedPoint14 {
    std::int32_t X, Y, Z;
    std::uint16_t intensity;
    // byte 14: legacy return info
    std::uint8_t legacy_return_number : 3;
    std::uint8_t legacy_number_of_returns : 3;
    std::uint8_t scan_direction_flag : 1;
    std::uint8_t edge_of_flight_line : 1;
    // byte 15: legacy classification + flags
    std::uint8_t legacy_classification : 5;
    std::uint8_t legacy_flags : 3;
    // byte 16
    std::int8_t legacy_scan_angle_rank;
    // byte 17
    std::uint8_t user_data;
    // bytes 18-19
    std::uint16_t point_source_ID;
    // bytes 20-21
    std::int16_t scan_angle;
    // byte 22: extended fields
    std::uint8_t legacy_point_type : 2;
    std::uint8_t scanner_channel : 2;
    std::uint8_t classification_flags : 4;
    // byte 23
    std::uint8_t classification;
    // byte 24: return numbers (extended)
    std::uint8_t return_number : 4;
    std::uint8_t number_of_returns : 4;
    // bytes 25-27: alignment padding
    std::uint8_t dummy[3];
    // byte 28: gps_time_change flag (used internally during compression)
    bool gps_time_change;
    // bytes 29-30: more padding for double alignment
    std::uint8_t dummy2[2];
    // bytes 32-39: gps_time
    double gps_time;
    // bytes 40-47: rgb + nir
    std::uint16_t rgb[4];
};
#pragma pack(pop)

struct LAScontextPOINT14 {
    bool unused{true};
    std::uint8_t last_item[128]{};
    std::uint16_t last_intensity[8]{};
    StreamingMedian5 last_X_diff_median5[12];
    StreamingMedian5 last_Y_diff_median5[12];
    std::int32_t last_Z[8]{};

    std::unique_ptr<ArithmeticModel[]> m_changed_values;
    std::unique_ptr<ArithmeticModel> m_scanner_channel;
    std::unique_ptr<ArithmeticModel[]> m_number_of_returns;
    std::unique_ptr<ArithmeticModel[]> m_return_number;
    std::unique_ptr<ArithmeticModel> m_return_number_gps_same;

    std::unique_ptr<IntegerCompressor> ic_dX;
    std::unique_ptr<IntegerCompressor> ic_dY;
    std::unique_ptr<IntegerCompressor> ic_Z;
    std::unique_ptr<IntegerCompressor> ic_intensity;
    std::unique_ptr<IntegerCompressor> ic_scan_angle;
    std::unique_ptr<IntegerCompressor> ic_point_source_ID;
    std::unique_ptr<IntegerCompressor> ic_gpstime;

    std::unique_ptr<ArithmeticModel[]> m_classification;
    std::unique_ptr<ArithmeticModel[]> m_flags;
    std::unique_ptr<ArithmeticModel[]> m_user_data;

    // GPS time state
    std::uint32_t last_gpstime_seq{0};
    std::uint32_t next_gpstime_seq{0};
    U64I64F64 last_gpstime[4]{};
    std::int32_t last_gpstime_diff[4]{};
    std::int32_t multi_extreme_counter[4]{};
    std::unique_ptr<ArithmeticModel> m_gpstime_multi;
    std::unique_ptr<ArithmeticModel> m_gpstime_0diff;
};

struct LAScontextRGB14 {
    bool unused{true};
    std::uint16_t last_item[3]{};

    std::unique_ptr<ArithmeticModel> m_byte_used;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_0;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_1;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_2;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_3;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_4;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_5;
};

struct LAScontextRGBNIR14 {
    bool unused{true};
    std::uint16_t last_item[4]{};

    std::unique_ptr<ArithmeticModel> m_byte_used;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_0;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_1;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_2;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_3;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_4;
    std::unique_ptr<ArithmeticModel> m_rgb_diff_5;

    std::unique_ptr<ArithmeticModel> m_nir_bytes_used;
    std::unique_ptr<ArithmeticModel> m_nir_diff_0;
    std::unique_ptr<ArithmeticModel> m_nir_diff_1;
};

inline constexpr std::int32_t GPSTIME_MULTI = 500;
inline constexpr std::int32_t GPSTIME_MULTI_MINUS = -10;
inline constexpr std::int32_t GPSTIME_MULTI_CODE_FULL = GPSTIME_MULTI - GPSTIME_MULTI_MINUS + 1;
inline constexpr std::int32_t GPSTIME_MULTI_TOTAL = GPSTIME_MULTI - GPSTIME_MULTI_MINUS + 5;

inline constexpr std::uint32_t POINT14_NUM_LAYERS = 9;

enum Point14Layer : std::uint32_t {
    LAYER_CHANNEL_RETURNS_XY = 0,
    LAYER_Z                  = 1,
    LAYER_CLASSIFICATION     = 2,
    LAYER_FLAGS              = 3,
    LAYER_INTENSITY          = 4,
    LAYER_SCAN_ANGLE         = 5,
    LAYER_USER_DATA          = 6,
    LAYER_POINT_SOURCE       = 7,
    LAYER_GPSTIME            = 8,
};

} // namespace picolaz
