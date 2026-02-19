#include "picolaz/laz_reader.hpp"
#include <algorithm>
#include <cstring>

namespace picolaz {

LazReader::LazReader(const std::string& path) {
    file_.open(path, std::ios::binary);
    if (!file_.is_open()) throw LazError("cannot open file for reading: " + path);

    read_header();
    find_laszip_vlr();
    read_chunk_table();

    file_.seekg(header_.offset_to_point_data);
}

LazReader::~LazReader() {
    if (!closed_) close();
}

void LazReader::read_header() {
    file_.read(reinterpret_cast<char*>(&header_), sizeof(LasHeader));
    if (!file_.good()) throw LazError("failed to read LAS header");

    if (std::memcmp(header_.file_signature, "LASF", 4) != 0) {
        throw LazError("invalid LAS file signature");
    }

    if (header_.version_major != 1 || header_.version_minor != 4) {
        throw LazError("only LAS 1.4 is supported");
    }

    u8 fmt = header_.point_data_record_format;
    if (fmt < 6 || fmt > 8) throw LazError("unsupported point format");
    format_ = static_cast<PointFormat>(fmt);

    if (format_has_nir(format_)) {
        rgbnir_decomp_ = std::make_unique<RGBNIR14Decompressor>();
    } else if (format_has_rgb(format_)) {
        rgb_decomp_ = std::make_unique<RGB14Decompressor>();
    }
}

void LazReader::find_laszip_vlr() {
    file_.seekg(sizeof(LasHeader));

    for (u32 i = 0; i < header_.number_of_vlrs; ++i) {
        VlrHeader vlr_hdr;
        file_.read(reinterpret_cast<char*>(&vlr_hdr), sizeof(VlrHeader));
        if (!file_.good()) throw LazError("failed to read VLR header");

        if (std::strncmp(vlr_hdr.user_id, "laszip encoded", 14) == 0 &&
            vlr_hdr.record_id == 22204) {

            file_.read(reinterpret_cast<char*>(&laszip_vlr_.compressor), 2);
            file_.read(reinterpret_cast<char*>(&laszip_vlr_.coder), 2);
            file_.read(reinterpret_cast<char*>(&laszip_vlr_.version_major), 1);
            file_.read(reinterpret_cast<char*>(&laszip_vlr_.version_minor), 1);
            file_.read(reinterpret_cast<char*>(&laszip_vlr_.version_revision), 2);
            file_.read(reinterpret_cast<char*>(&laszip_vlr_.options), 4);
            file_.read(reinterpret_cast<char*>(&laszip_vlr_.chunk_size), 4);
            file_.read(reinterpret_cast<char*>(&laszip_vlr_.num_special_evlrs), 8);
            file_.read(reinterpret_cast<char*>(&laszip_vlr_.offset_special_evlrs), 8);
            file_.read(reinterpret_cast<char*>(&laszip_vlr_.num_items), 2);

            laszip_vlr_.items.resize(laszip_vlr_.num_items);
            for (u16 j = 0; j < laszip_vlr_.num_items; ++j) {
                file_.read(reinterpret_cast<char*>(&laszip_vlr_.items[j].type), 2);
                file_.read(reinterpret_cast<char*>(&laszip_vlr_.items[j].size), 2);
                file_.read(reinterpret_cast<char*>(&laszip_vlr_.items[j].version), 2);
            }
            return;
        }

        file_.seekg(vlr_hdr.record_length_after_header, std::ios::cur);
    }

    throw LazError("LASzip VLR not found");
}

void LazReader::read_chunk_table() {
    if (header_.start_of_first_evlr == 0 || header_.number_of_evlrs == 0) {
        throw LazError("no EVLR found for chunk table");
    }

    file_.seekg(static_cast<std::streamoff>(header_.start_of_first_evlr));

    EvlrHeader evlr_hdr;
    file_.read(reinterpret_cast<char*>(&evlr_hdr), sizeof(EvlrHeader));
    if (!file_.good()) throw LazError("failed to read EVLR header");

    u32 version, count;
    file_.read(reinterpret_cast<char*>(&version), 4);
    file_.read(reinterpret_cast<char*>(&count), 4);

    chunk_table_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        file_.read(reinterpret_cast<char*>(&chunk_table_[i]), 4);
    }

    current_chunk_ = 0;
}

bool LazReader::start_next_chunk() {
    if (current_chunk_ >= chunk_table_.size()) return false;

    u32 chunk_byte_size = chunk_table_[current_chunk_];
    chunk_data_.resize(chunk_byte_size);
    file_.read(reinterpret_cast<char*>(chunk_data_.data()), chunk_byte_size);
    if (!file_.good()) return false;

    chunk_in_.init(chunk_data_.data(), chunk_byte_size);

    point_decomp_.chunk_sizes(&chunk_in_);
    if (rgb_decomp_) rgb_decomp_->chunk_sizes(&chunk_in_);
    if (rgbnir_decomp_) rgbnir_decomp_->chunk_sizes(&chunk_in_);

    point_decomp_.init(&chunk_in_);
    if (rgb_decomp_) rgb_decomp_->init(&chunk_in_);
    if (rgbnir_decomp_) rgbnir_decomp_->init(&chunk_in_);

    // Determine points in this chunk
    u64 remaining = header_.number_of_point_records - total_points_read_;
    points_in_chunk_ = static_cast<u32>(std::min(static_cast<u64>(laszip_vlr_.chunk_size), remaining));
    points_read_in_chunk_ = 0;

    ++current_chunk_;
    return true;
}

bool LazReader::read_point(Point14& pt) {
    if (total_points_read_ >= header_.number_of_point_records) return false;

    if (points_read_in_chunk_ >= points_in_chunk_) {
        if (!start_next_chunk()) return false;
    }

    PackedPoint14 packed;
    std::memset(&packed, 0, sizeof(packed));
    point_decomp_.read(reinterpret_cast<u8*>(&packed));

    if (format_has_nir(format_)) {
        u16 rgbnir[4]{};
        rgbnir_decomp_->read(rgbnir);
        packed.rgb[0] = rgbnir[0];
        packed.rgb[1] = rgbnir[1];
        packed.rgb[2] = rgbnir[2];
        packed.rgb[3] = rgbnir[3];
    } else if (format_has_rgb(format_)) {
        u16 rgb[3]{};
        rgb_decomp_->read(rgb);
        packed.rgb[0] = rgb[0];
        packed.rgb[1] = rgb[1];
        packed.rgb[2] = rgb[2];
    }

    packed_to_point(packed, pt);

    ++points_read_in_chunk_;
    ++total_points_read_;
    return true;
}

std::vector<Point14> LazReader::read_points(u64 count) {
    std::vector<Point14> result;
    result.reserve(static_cast<size_t>(count));
    Point14 pt;
    for (u64 i = 0; i < count; ++i) {
        if (!read_point(pt)) break;
        result.push_back(pt);
    }
    return result;
}

void LazReader::close() {
    if (closed_) return;
    closed_ = true;
    file_.close();
}

void LazReader::packed_to_point(const PackedPoint14& p, Point14& pt) {
    pt.x = p.X;
    pt.y = p.Y;
    pt.z = p.Z;
    pt.intensity = p.intensity;
    pt.return_number = p.return_number;
    pt.number_of_returns = p.number_of_returns;
    pt.classification_flags = p.classification_flags;
    pt.scanner_channel = p.scanner_channel;
    pt.scan_direction_flag = p.scan_direction_flag;
    pt.edge_of_flight_line = p.edge_of_flight_line;
    pt.classification = p.classification;
    pt.user_data = p.user_data;
    pt.scan_angle = p.scan_angle;
    pt.point_source_id = p.point_source_ID;
    pt.gps_time = p.gps_time;
    pt.red = p.rgb[0];
    pt.green = p.rgb[1];
    pt.blue = p.rgb[2];
    pt.nir = p.rgb[3];
}

} // namespace picolaz
