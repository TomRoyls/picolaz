#pragma once

#include "types.hpp"
#include "point14_compressor.hpp"
#include "rgb14_compressor.hpp"
#include "rgbnir14_compressor.hpp"
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace picolaz {

class LazWriter {
public:
    struct Config {
        PointFormat format{PointFormat::Format6};
        u32 chunk_size{50000};
        f64 x_scale{0.001};
        f64 y_scale{0.001};
        f64 z_scale{0.001};
        f64 x_offset{0.0};
        f64 y_offset{0.0};
        f64 z_offset{0.0};
    };

    explicit LazWriter(const std::string& path, const Config& config);
    ~LazWriter();

    LazWriter(const LazWriter&) = delete;
    LazWriter& operator=(const LazWriter&) = delete;

    void write_point(const Point14& pt);
    void write_points(std::span<const Point14> pts);
    void close();

private:
    void write_header();
    void write_laszip_vlr();
    void start_chunk();
    void finish_chunk();
    void write_chunk_table_evlr();
    void point_to_packed(const Point14& pt, PackedPoint14& packed);
    void update_bounds(const Point14& pt);

    Config config_;
    std::ofstream file_;
    LasHeader header_;
    LaszipVlr laszip_vlr_;

    Point14Compressor point_comp_;
    std::unique_ptr<RGB14Compressor> rgb_comp_;
    std::unique_ptr<RGBNIR14Compressor> rgbnir_comp_;

    ByteStreamOut chunk_out_;
    std::vector<u32> chunk_table_;
    u32 points_in_chunk_{0};
    u64 total_points_{0};
    bool closed_{false};
    bool chunk_started_{false};

    f64 min_x_{1e300}, min_y_{1e300}, min_z_{1e300};
    f64 max_x_{-1e300}, max_y_{-1e300}, max_z_{-1e300};
};

} // namespace picolaz
