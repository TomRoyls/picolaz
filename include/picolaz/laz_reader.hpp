#pragma once

#include "types.hpp"
#include "laz14_v3.hpp"
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace picolaz {

class LazReader {
public:
    explicit LazReader(const std::string& path);
    ~LazReader();

    LazReader(const LazReader&) = delete;
    LazReader& operator=(const LazReader&) = delete;

    const LasHeader& header() const { return header_; }
    PointFormat format() const { return format_; }
    std::uint64_t point_count() const { return header_.number_of_point_records; }

    bool read_point(Point14& pt);
    std::vector<Point14> read_points(std::uint64_t count);
    void close();

private:
    void read_header();
    void find_laszip_vlr();
    void read_chunk_table();
    bool start_next_chunk();
    void record_to_point(const std::uint8_t* rec, Point14& pt);

    std::ifstream file_;
    LasHeader header_;
    PointFormat format_{PointFormat::Format6};
    std::uint32_t chunk_size_{50000};

    v3::Point14DecompressorV3 point_decomp_;
    std::unique_ptr<v3::Rgb14DecompressorV3> rgb_decomp_;
    std::unique_ptr<v3::RgbNir14DecompressorV3> rgbnir_decomp_;

    std::vector<std::uint64_t> chunk_offsets_;
    std::vector<std::uint64_t> chunk_sizes_;
    std::uint32_t current_chunk_{0};
    std::uint32_t points_in_chunk_{0};
    std::uint32_t points_read_in_chunk_{0};
    std::uint64_t total_points_read_{0};
    bool closed_{false};
    bool rgb_layer_{false};
    bool nir_layer_{false};

    std::vector<std::uint8_t> chunk_data_;
    std::uint64_t chunk_pos_{0};
    std::uint8_t first_record_[38]{};
    bool first_pending_{false};
};

}
