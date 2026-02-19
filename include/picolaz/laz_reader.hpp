#pragma once

#include "types.hpp"
#include "point14_decompressor.hpp"
#include "rgb14_decompressor.hpp"
#include "rgbnir14_decompressor.hpp"
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
    u64 point_count() const { return header_.number_of_point_records; }

    bool read_point(Point14& pt);
    std::vector<Point14> read_points(u64 count);
    void close();

private:
    void read_header();
    void find_laszip_vlr();
    void read_chunk_table();
    bool start_next_chunk();
    void packed_to_point(const PackedPoint14& p, Point14& pt);

    std::ifstream file_;
    LasHeader header_;
    PointFormat format_{PointFormat::Format6};
    LaszipVlr laszip_vlr_;

    Point14Decompressor point_decomp_;
    std::unique_ptr<RGB14Decompressor> rgb_decomp_;
    std::unique_ptr<RGBNIR14Decompressor> rgbnir_decomp_;

    std::vector<u32> chunk_table_;
    u32 current_chunk_{0};
    u32 points_in_chunk_{0};
    u32 points_read_in_chunk_{0};
    u64 total_points_read_{0};
    bool closed_{false};

    std::vector<u8> chunk_data_;
    ByteStreamIn chunk_in_;
};

} // namespace picolaz
