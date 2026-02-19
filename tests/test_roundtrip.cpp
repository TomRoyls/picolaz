#include <catch2/catch_test_macros.hpp>
#include <picolaz/laz_writer.hpp>
#include <picolaz/laz_reader.hpp>
#include <filesystem>
#include <vector>
#include <cstdlib>
#include <cmath>

using namespace picolaz;

namespace {

struct TempFile {
    std::filesystem::path path;
    TempFile(const std::string& suffix = ".laz")
        : path(std::filesystem::temp_directory_path() / ("picolaz_test_" + std::to_string(std::rand()) + suffix)) {}
    ~TempFile() { std::filesystem::remove(path); }
    std::string str() const { return path.string(); }
};

Point14 make_point(i32 x, i32 y, i32 z, f64 gps_time = 0.0) {
    Point14 pt;
    pt.x = x;
    pt.y = y;
    pt.z = z;
    pt.gps_time = gps_time;
    return pt;
}

Point14 make_full_point(int i) {
    Point14 pt;
    pt.x = 1000 + i * 10;
    pt.y = 2000 + i * 5;
    pt.z = 500 + (i % 100);
    pt.intensity = static_cast<u16>(i % 65536);
    pt.return_number = static_cast<u8>((i % 4) + 1);
    pt.number_of_returns = static_cast<u8>((i % 4) + 1);
    pt.classification_flags = static_cast<u8>(i % 16);
    pt.scanner_channel = static_cast<u8>(i % 4);
    pt.scan_direction_flag = static_cast<u8>(i % 2);
    pt.edge_of_flight_line = static_cast<u8>((i + 1) % 2);
    pt.classification = static_cast<u8>(i % 256);
    pt.user_data = static_cast<u8>((i * 7) % 256);
    pt.scan_angle = static_cast<i16>((i % 1000) - 500);
    pt.point_source_id = static_cast<u16>(i % 10);
    pt.gps_time = 1000000.0 + i * 0.001;
    pt.red = static_cast<u16>((i * 3) % 65536);
    pt.green = static_cast<u16>((i * 5) % 65536);
    pt.blue = static_cast<u16>((i * 7) % 65536);
    pt.nir = static_cast<u16>((i * 11) % 65536);
    return pt;
}

void check_point_equal(const Point14& a, const Point14& b, bool has_rgb = false, bool has_nir = false) {
    CHECK(a.x == b.x);
    CHECK(a.y == b.y);
    CHECK(a.z == b.z);
    CHECK(a.intensity == b.intensity);
    CHECK(a.return_number == b.return_number);
    CHECK(a.number_of_returns == b.number_of_returns);
    CHECK(a.classification_flags == b.classification_flags);
    CHECK(a.scanner_channel == b.scanner_channel);
    CHECK(a.scan_direction_flag == b.scan_direction_flag);
    CHECK(a.edge_of_flight_line == b.edge_of_flight_line);
    CHECK(a.classification == b.classification);
    CHECK(a.user_data == b.user_data);
    CHECK(a.scan_angle == b.scan_angle);
    CHECK(a.point_source_id == b.point_source_id);
    CHECK(a.gps_time == b.gps_time);
    if (has_rgb) {
        CHECK(a.red == b.red);
        CHECK(a.green == b.green);
        CHECK(a.blue == b.blue);
    }
    if (has_nir) {
        CHECK(a.nir == b.nir);
    }
}

} // namespace

TEST_CASE("Roundtrip single point format 6", "[roundtrip]") {
    TempFile tmp;
    Point14 pt = make_full_point(42);

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format6;
        LazWriter writer(tmp.str(), cfg);
        writer.write_point(pt);
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        CHECK(reader.point_count() == 1);
        CHECK(reader.format() == PointFormat::Format6);

        Point14 read_pt;
        CHECK(reader.read_point(read_pt));
        check_point_equal(pt, read_pt);

        CHECK_FALSE(reader.read_point(read_pt));
    }
}

TEST_CASE("Roundtrip 100 points format 6 single chunk", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 100;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_full_point(i);
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format6;
        cfg.chunk_size = 50000;
        LazWriter writer(tmp.str(), cfg);
        for (auto& pt : points) {
            writer.write_point(pt);
        }
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        CHECK(reader.point_count() == N);

        for (int i = 0; i < N; i++) {
            Point14 read_pt;
            REQUIRE(reader.read_point(read_pt));
            check_point_equal(points[i], read_pt);
        }
    }
}

TEST_CASE("Roundtrip 100000 points format 6 multiple chunks", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 100000;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_full_point(i);
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format6;
        cfg.chunk_size = 50000;
        LazWriter writer(tmp.str(), cfg);
        for (auto& pt : points) {
            writer.write_point(pt);
        }
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        CHECK(reader.point_count() == N);

        for (int i = 0; i < N; i++) {
            Point14 read_pt;
            REQUIRE(reader.read_point(read_pt));
            check_point_equal(points[i], read_pt);
        }
    }
}

TEST_CASE("Roundtrip format 7 (RGB)", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 200;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_full_point(i);
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format7;
        LazWriter writer(tmp.str(), cfg);
        for (auto& pt : points) {
            writer.write_point(pt);
        }
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        CHECK(reader.point_count() == N);
        CHECK(reader.format() == PointFormat::Format7);

        for (int i = 0; i < N; i++) {
            Point14 read_pt;
            REQUIRE(reader.read_point(read_pt));
            check_point_equal(points[i], read_pt, true);
        }
    }
}

TEST_CASE("Roundtrip format 8 (RGB+NIR)", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 200;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_full_point(i);
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format8;
        LazWriter writer(tmp.str(), cfg);
        for (auto& pt : points) {
            writer.write_point(pt);
        }
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        CHECK(reader.point_count() == N);
        CHECK(reader.format() == PointFormat::Format8);

        for (int i = 0; i < N; i++) {
            Point14 read_pt;
            REQUIRE(reader.read_point(read_pt));
            check_point_equal(points[i], read_pt, true, true);
        }
    }
}

TEST_CASE("Roundtrip verifies header fields", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 50;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_full_point(i);
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format7;
        cfg.x_scale = 0.01;
        cfg.y_scale = 0.01;
        cfg.z_scale = 0.01;
        cfg.x_offset = 100.0;
        cfg.y_offset = 200.0;
        cfg.z_offset = 50.0;
        LazWriter writer(tmp.str(), cfg);
        for (auto& pt : points) {
            writer.write_point(pt);
        }
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        const auto& hdr = reader.header();

        CHECK(hdr.number_of_point_records == N);
        CHECK(hdr.point_data_record_format == 7);
        CHECK(hdr.point_data_record_length == 36);
        CHECK(hdr.version_major == 1);
        CHECK(hdr.version_minor == 4);
        CHECK(hdr.x_scale_factor == 0.01);
        CHECK(hdr.y_scale_factor == 0.01);
        CHECK(hdr.z_scale_factor == 0.01);
        CHECK(hdr.x_offset == 100.0);
        CHECK(hdr.y_offset == 200.0);
        CHECK(hdr.z_offset == 50.0);

        CHECK(hdr.min_x <= hdr.max_x);
        CHECK(hdr.min_y <= hdr.max_y);
        CHECK(hdr.min_z <= hdr.max_z);
    }
}

TEST_CASE("Roundtrip small chunk size", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 55;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_full_point(i);
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format6;
        cfg.chunk_size = 10;
        LazWriter writer(tmp.str(), cfg);
        for (auto& pt : points) {
            writer.write_point(pt);
        }
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        CHECK(reader.point_count() == N);

        for (int i = 0; i < N; i++) {
            Point14 read_pt;
            REQUIRE(reader.read_point(read_pt));
            check_point_equal(points[i], read_pt);
        }
    }
}

TEST_CASE("Roundtrip varying scanner channels", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 100;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_point(i * 100, i * 200, i * 50, 1000.0 + i);
        points[i].scanner_channel = static_cast<u8>(i % 4);
        points[i].return_number = 1;
        points[i].number_of_returns = 1;
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format6;
        LazWriter writer(tmp.str(), cfg);
        for (auto& pt : points) {
            writer.write_point(pt);
        }
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        for (int i = 0; i < N; i++) {
            Point14 read_pt;
            REQUIRE(reader.read_point(read_pt));
            CHECK(read_pt.scanner_channel == points[i].scanner_channel);
            CHECK(read_pt.x == points[i].x);
            CHECK(read_pt.y == points[i].y);
            CHECK(read_pt.z == points[i].z);
        }
    }
}

TEST_CASE("Roundtrip varying classifications", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 256;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_point(i, i, i, 1000.0 + i);
        points[i].classification = static_cast<u8>(i);
        points[i].return_number = 1;
        points[i].number_of_returns = 1;
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format6;
        LazWriter writer(tmp.str(), cfg);
        for (auto& pt : points) {
            writer.write_point(pt);
        }
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        for (int i = 0; i < N; i++) {
            Point14 read_pt;
            REQUIRE(reader.read_point(read_pt));
            CHECK(read_pt.classification == points[i].classification);
        }
    }
}

TEST_CASE("Roundtrip batch write_points and read_points", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 500;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_full_point(i);
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format6;
        LazWriter writer(tmp.str(), cfg);
        writer.write_points(points);
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        CHECK(reader.point_count() == N);

        auto read_pts = reader.read_points(N);
        REQUIRE(read_pts.size() == N);

        for (int i = 0; i < N; i++) {
            check_point_equal(points[i], read_pts[i]);
        }
    }
}

TEST_CASE("Roundtrip batch write format 8 with read_points", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 300;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_full_point(i);
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format8;
        LazWriter writer(tmp.str(), cfg);
        writer.write_points(points);
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        auto read_pts = reader.read_points(N);
        REQUIRE(read_pts.size() == N);

        for (int i = 0; i < N; i++) {
            check_point_equal(points[i], read_pts[i], true, true);
        }
    }
}

TEST_CASE("Roundtrip all point fields survive", "[roundtrip]") {
    TempFile tmp;

    Point14 pt;
    pt.x = 123456;
    pt.y = -654321;
    pt.z = 99999;
    pt.intensity = 50000;
    pt.return_number = 3;
    pt.number_of_returns = 5;
    pt.classification_flags = 0x0A;
    pt.scanner_channel = 2;
    pt.scan_direction_flag = 1;
    pt.edge_of_flight_line = 1;
    pt.classification = 6;
    pt.user_data = 0xAB;
    pt.scan_angle = -15000;
    pt.point_source_id = 42;
    pt.gps_time = 123456789.123456;
    pt.red = 60000;
    pt.green = 30000;
    pt.blue = 10000;
    pt.nir = 45000;

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format8;
        LazWriter writer(tmp.str(), cfg);
        writer.write_point(pt);
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        Point14 read_pt;
        REQUIRE(reader.read_point(read_pt));

        CHECK(read_pt.x == 123456);
        CHECK(read_pt.y == -654321);
        CHECK(read_pt.z == 99999);
        CHECK(read_pt.intensity == 50000);
        CHECK(read_pt.return_number == 3);
        CHECK(read_pt.number_of_returns == 5);
        CHECK(read_pt.classification_flags == 0x0A);
        CHECK(read_pt.scanner_channel == 2);
        CHECK(read_pt.scan_direction_flag == 1);
        CHECK(read_pt.edge_of_flight_line == 1);
        CHECK(read_pt.classification == 6);
        CHECK(read_pt.user_data == 0xAB);
        CHECK(read_pt.scan_angle == -15000);
        CHECK(read_pt.point_source_id == 42);
        CHECK(read_pt.gps_time == 123456789.123456);
        CHECK(read_pt.red == 60000);
        CHECK(read_pt.green == 30000);
        CHECK(read_pt.blue == 10000);
        CHECK(read_pt.nir == 45000);
    }
}

TEST_CASE("Roundtrip with multiple small chunks format 7", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 35;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_full_point(i);
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format7;
        cfg.chunk_size = 10;
        LazWriter writer(tmp.str(), cfg);
        writer.write_points(points);
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        CHECK(reader.point_count() == N);

        auto read_pts = reader.read_points(N);
        REQUIRE(read_pts.size() == N);

        for (int i = 0; i < N; i++) {
            check_point_equal(points[i], read_pts[i], true);
        }
    }
}

TEST_CASE("Roundtrip points with zero GPS time", "[roundtrip]") {
    TempFile tmp;
    constexpr int N = 50;

    std::vector<Point14> points(N);
    for (int i = 0; i < N; i++) {
        points[i] = make_point(i * 100, i * 200, i * 50);
        points[i].return_number = 1;
        points[i].number_of_returns = 1;
    }

    {
        LazWriter::Config cfg;
        cfg.format = PointFormat::Format6;
        LazWriter writer(tmp.str(), cfg);
        writer.write_points(points);
        writer.close();
    }

    {
        LazReader reader(tmp.str());
        auto read_pts = reader.read_points(N);
        REQUIRE(read_pts.size() == N);

        for (int i = 0; i < N; i++) {
            CHECK(read_pts[i].gps_time == 0.0);
            CHECK(read_pts[i].x == points[i].x);
            CHECK(read_pts[i].y == points[i].y);
            CHECK(read_pts[i].z == points[i].z);
        }
    }
}
