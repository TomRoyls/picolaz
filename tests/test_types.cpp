#include <catch2/catch_test_macros.hpp>
#include <picolaz/types.hpp>

using namespace picolaz;

TEST_CASE("point_record_length returns correct sizes", "[types]") {
    CHECK(point_record_length(PointFormat::Format6) == 30);
    CHECK(point_record_length(PointFormat::Format7) == 36);
    CHECK(point_record_length(PointFormat::Format8) == 38);
}

TEST_CASE("format_has_rgb", "[types]") {
    CHECK_FALSE(format_has_rgb(PointFormat::Format6));
    CHECK(format_has_rgb(PointFormat::Format7));
    CHECK(format_has_rgb(PointFormat::Format8));
}

TEST_CASE("format_has_nir", "[types]") {
    CHECK_FALSE(format_has_nir(PointFormat::Format6));
    CHECK_FALSE(format_has_nir(PointFormat::Format7));
    CHECK(format_has_nir(PointFormat::Format8));
}

TEST_CASE("LasHeader size is 375 bytes", "[types]") {
    CHECK(sizeof(LasHeader) == 375);
}

TEST_CASE("VlrHeader size is 54 bytes", "[types]") {
    CHECK(sizeof(VlrHeader) == 54);
}

TEST_CASE("EvlrHeader size is 60 bytes", "[types]") {
    CHECK(sizeof(EvlrHeader) == 60);
}

TEST_CASE("u32_zero_bit_0 clears lowest bit", "[types]") {
    CHECK(u32_zero_bit_0(0) == 0);
    CHECK(u32_zero_bit_0(1) == 0);
    CHECK(u32_zero_bit_0(2) == 2);
    CHECK(u32_zero_bit_0(3) == 2);
    CHECK(u32_zero_bit_0(0xFF) == 0xFE);
    CHECK(u32_zero_bit_0(0xFFFFFFFF) == 0xFFFFFFFE);
}

TEST_CASE("i32_quantize rounds correctly", "[types]") {
    CHECK(i32_quantize(0.0f) == 0);
    CHECK(i32_quantize(0.4f) == 0);
    CHECK(i32_quantize(0.5f) == 1);
    CHECK(i32_quantize(0.6f) == 1);
    CHECK(i32_quantize(1.5f) == 2);
    CHECK(i32_quantize(-0.4f) == 0);
    CHECK(i32_quantize(-0.5f) == -1);
    CHECK(i32_quantize(-0.6f) == -1);
    CHECK(i32_quantize(-1.5f) == -2);
    CHECK(i32_quantize(100.3f) == 100);
    CHECK(i32_quantize(-100.3f) == -100);
}

TEST_CASE("i8_clamp clamps to [-128, 127]", "[types]") {
    CHECK(i8_clamp(0) == 0);
    CHECK(i8_clamp(127) == 127);
    CHECK(i8_clamp(128) == 127);
    CHECK(i8_clamp(1000) == 127);
    CHECK(i8_clamp(-128) == -128);
    CHECK(i8_clamp(-129) == -128);
    CHECK(i8_clamp(-1000) == -128);
    CHECK(i8_clamp(50) == 50);
    CHECK(i8_clamp(-50) == -50);
}

TEST_CASE("LasHeader default values", "[types]") {
    LasHeader h;
    CHECK(h.file_signature[0] == 'L');
    CHECK(h.file_signature[1] == 'A');
    CHECK(h.file_signature[2] == 'S');
    CHECK(h.file_signature[3] == 'F');
    CHECK(h.version_major == 1);
    CHECK(h.version_minor == 4);
    CHECK(h.header_size == 375);
    CHECK(h.point_data_record_format == 6);
    CHECK(h.point_data_record_length == 30);
    CHECK(h.x_scale_factor == 0.001);
    CHECK(h.y_scale_factor == 0.001);
    CHECK(h.z_scale_factor == 0.001);
}

TEST_CASE("Point14 default values are zero", "[types]") {
    Point14 pt;
    CHECK(pt.x == 0);
    CHECK(pt.y == 0);
    CHECK(pt.z == 0);
    CHECK(pt.intensity == 0);
    CHECK(pt.return_number == 0);
    CHECK(pt.number_of_returns == 0);
    CHECK(pt.classification == 0);
    CHECK(pt.gps_time == 0.0);
    CHECK(pt.red == 0);
    CHECK(pt.green == 0);
    CHECK(pt.blue == 0);
    CHECK(pt.nir == 0);
}

TEST_CASE("U32I32F32 union type-punning", "[types]") {
    U32I32F32 v;
    v.u = 0;
    CHECK(v.i == 0);
    v.i = -1;
    CHECK(v.u == 0xFFFFFFFF);
}

TEST_CASE("U64I64F64 union type-punning", "[types]") {
    U64I64F64 v;
    v.u = 0;
    CHECK(v.i == 0);
    v.i = -1;
    CHECK(v.u == 0xFFFFFFFFFFFFFFFF);
}
