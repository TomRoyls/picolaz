// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "picolaz/laz_writer.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace picolaz {

LazWriter::LazWriter(const std::string& path, const Config& config)
    : config_(config) {
    file_.open(path, std::ios::binary);
    if (!file_.is_open()) throw LazError("cannot open file for writing: " + path);

    if (format_has_nir(config_.format)) {
        rgbnir_comp_ = std::make_unique<RGBNIR14Compressor>();
    } else if (format_has_rgb(config_.format)) {
        rgb_comp_ = std::make_unique<RGB14Compressor>();
    }

    write_header();
    write_laszip_vlr();

    // Record where point data starts
    header_.offset_to_point_data = static_cast<std::uint32_t>(file_.tellp());

    // Rewrite header with correct offset
    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(&header_), sizeof(LasHeader));
    file_.seekp(header_.offset_to_point_data);
}

LazWriter::~LazWriter() {
    if (!closed_) {
        try { close(); } catch (...) {}
    }
}

void LazWriter::write_header() {
    header_ = LasHeader{}; // Value-initialization
    header_.file_signature[0] = 'L';
    header_.file_signature[1] = 'A';
    header_.file_signature[2] = 'S';
    header_.file_signature[3] = 'F';
    header_.file_source_id = 0;
    header_.global_encoding = 0x10; // WKT bit
    header_.version_major = 1;
    header_.version_minor = 4;

    const char* gen_sw = "picolaz";
    std::strncpy(header_.generating_software, gen_sw, 31);

    header_.header_size = 375;
    header_.offset_to_point_data = 375;
    header_.number_of_vlrs = 1;
    header_.point_data_record_format = static_cast<std::uint8_t>(config_.format);
    header_.point_data_record_length = point_record_length(config_.format);

    header_.x_scale_factor = config_.x_scale;
    header_.y_scale_factor = config_.y_scale;
    header_.z_scale_factor = config_.z_scale;
    header_.x_offset = config_.x_offset;
    header_.y_offset = config_.y_offset;
    header_.z_offset = config_.z_offset;

    file_.write(reinterpret_cast<const char*>(&header_), sizeof(LasHeader));
}

void LazWriter::write_laszip_vlr() {
    laszip_vlr_.compressor = 3;
    laszip_vlr_.coder = 0;
    laszip_vlr_.version_major = 3;
    laszip_vlr_.version_minor = 4;
    laszip_vlr_.version_revision = 0;
    laszip_vlr_.options = 0;
    laszip_vlr_.chunk_size = config_.chunk_size;
    laszip_vlr_.num_special_evlrs = -1;
    laszip_vlr_.offset_special_evlrs = -1;

    laszip_vlr_.items.clear();

    // POINT14 item
    LaszipItemRecord point_item;
    point_item.type = 6;
    point_item.size = 30;
    point_item.version = 4;
    laszip_vlr_.items.push_back(point_item);

    if (format_has_nir(config_.format)) {
        LaszipItemRecord rgbnir_item;
        rgbnir_item.type = 8;
        rgbnir_item.size = 8;
        rgbnir_item.version = 4;
        laszip_vlr_.items.push_back(rgbnir_item);
    } else if (format_has_rgb(config_.format)) {
        LaszipItemRecord rgb_item;
        rgb_item.type = 7;
        rgb_item.size = 6;
        rgb_item.version = 4;
        laszip_vlr_.items.push_back(rgb_item);
    }

    laszip_vlr_.num_items = static_cast<std::uint16_t>(laszip_vlr_.items.size());

    VlrHeader vlr_hdr{};
    std::strncpy(vlr_hdr.user_id, "laszip encoded", 15);
    vlr_hdr.record_id = 22204;

    // VLR payload: fixed fields + items
    // compressor(2) + coder(2) + ver_major(1) + ver_minor(1) + ver_rev(2) +
    // options(4) + chunk_size(4) + num_special_evlrs(8) + offset_special_evlrs(8) +
    // num_items(2) + items(6 each)
    std::uint16_t payload_size = 34 + static_cast<std::uint16_t>(laszip_vlr_.items.size() * 6);
    vlr_hdr.record_length_after_header = payload_size;

    std::strncpy(vlr_hdr.description, "picolaz by picolaz", 31);

    file_.write(reinterpret_cast<const char*>(&vlr_hdr), sizeof(VlrHeader));

    // Write VLR payload
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.compressor), 2);
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.coder), 2);
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.version_major), 1);
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.version_minor), 1);
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.version_revision), 2);
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.options), 4);
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.chunk_size), 4);
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.num_special_evlrs), 8);
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.offset_special_evlrs), 8);
    file_.write(reinterpret_cast<const char*>(&laszip_vlr_.num_items), 2);

    for (const auto& item : laszip_vlr_.items) {
        file_.write(reinterpret_cast<const char*>(&item.type), 2);
        file_.write(reinterpret_cast<const char*>(&item.size), 2);
        file_.write(reinterpret_cast<const char*>(&item.version), 2);
    }
}

void LazWriter::write_point(const Point14& pt) {
    if (!chunk_started_) start_chunk();

    update_bounds(pt);

    PackedPoint14 packed;
    std::memset(&packed, 0, sizeof(packed));
    point_to_packed(pt, packed);

    point_comp_.write(reinterpret_cast<const std::uint8_t*>(&packed));

    if (format_has_nir(config_.format)) {
        std::uint16_t rgbnir[4] = { pt.red, pt.green, pt.blue, pt.nir };
        rgbnir_comp_->write(rgbnir);
    } else if (format_has_rgb(config_.format)) {
        std::uint16_t rgb[3] = { pt.red, pt.green, pt.blue };
        rgb_comp_->write(rgb);
    }

    ++points_in_chunk_;
    ++total_points_;

    if (points_in_chunk_ >= config_.chunk_size) {
        finish_chunk();
    }
}

void LazWriter::write_points(std::span<const Point14> pts) {
    for (const auto& pt : pts) write_point(pt);
}

void LazWriter::start_chunk() {
    chunk_started_ = true;
    points_in_chunk_ = 0;
    chunk_out_.clear();

    point_comp_.init(&chunk_out_);
    if (rgb_comp_) rgb_comp_->init(&chunk_out_);
    if (rgbnir_comp_) rgbnir_comp_->init(&chunk_out_);
}

void LazWriter::finish_chunk() {
    if (!chunk_started_ || points_in_chunk_ == 0) return;

    // Write chunk sizes (layer sizes)
    point_comp_.chunk_sizes();
    if (rgb_comp_) rgb_comp_->chunk_sizes();
    if (rgbnir_comp_) rgbnir_comp_->chunk_sizes();

    // Write chunk bytes (layer data)
    point_comp_.chunk_bytes();
    if (rgb_comp_) rgb_comp_->chunk_bytes();
    if (rgbnir_comp_) rgbnir_comp_->chunk_bytes();

    // Record chunk size and write to file
    chunk_table_.push_back(chunk_out_.size());
    file_.write(reinterpret_cast<const char*>(chunk_out_.data()), chunk_out_.size());

    chunk_started_ = false;
}

void LazWriter::close() {
    if (closed_) return;
    closed_ = true;

    if (chunk_started_ && points_in_chunk_ > 0) {
        finish_chunk();
    }

    // Record EVLR position
    std::uint64_t evlr_offset = static_cast<std::uint64_t>(file_.tellp());

    write_chunk_table_evlr();

    // Patch header
    header_.number_of_point_records = total_points_;
    if (total_points_ <= 0xFFFFFFFF) {
        header_.legacy_number_of_point_records = static_cast<std::uint32_t>(total_points_);
    }
    header_.start_of_first_evlr = evlr_offset;
    header_.number_of_evlrs = 1;

    header_.min_x = min_x_;
    header_.max_x = max_x_;
    header_.min_y = min_y_;
    header_.max_y = max_y_;
    header_.min_z = min_z_;
    header_.max_z = max_z_;

    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(&header_), sizeof(LasHeader));

    file_.close();
}

void LazWriter::write_chunk_table_evlr() {
    EvlrHeader evlr_hdr{};
    std::strncpy(evlr_hdr.user_id, "laszip encoded", 15);
    evlr_hdr.record_id = 0;

    // Chunk table: version(std::uint32_t) + count(std::uint32_t) + (count+1) entries of std::uint32_t
    // Actually LASzip chunk table format: version(std::uint32_t) + count(std::uint32_t) + count entries of std::uint32_t
    std::uint32_t version = 0;
    std::uint32_t count = static_cast<std::uint32_t>(chunk_table_.size());
    std::uint64_t payload_size = 8 + static_cast<std::uint64_t>(count) * 4;
    evlr_hdr.record_length_after_header = payload_size;

    file_.write(reinterpret_cast<const char*>(&evlr_hdr), sizeof(EvlrHeader));
    file_.write(reinterpret_cast<const char*>(&version), 4);
    file_.write(reinterpret_cast<const char*>(&count), 4);

    for (std::uint32_t i = 0; i < count; ++i) {
        file_.write(reinterpret_cast<const char*>(&chunk_table_[i]), 4);
    }
}

void LazWriter::point_to_packed(const Point14& pt, PackedPoint14& p) {
    p.X = pt.x;
    p.Y = pt.y;
    p.Z = pt.z;
    p.intensity = pt.intensity;

    p.return_number = pt.return_number & 0x0F;
    p.number_of_returns = pt.number_of_returns & 0x0F;
    p.classification_flags = pt.classification_flags & 0x0F;
    p.scanner_channel = pt.scanner_channel & 0x03;
    p.scan_direction_flag = pt.scan_direction_flag & 0x01;
    p.edge_of_flight_line = pt.edge_of_flight_line & 0x01;
    p.classification = pt.classification;
    p.user_data = pt.user_data;
    p.scan_angle = pt.scan_angle;
    p.point_source_ID = pt.point_source_id;
    p.gps_time = pt.gps_time;
    p.gps_time_change = true;

    // Legacy fields
    p.legacy_return_number = std::min(static_cast<std::uint8_t>(pt.return_number), static_cast<std::uint8_t>(7));
    p.legacy_number_of_returns = std::min(static_cast<std::uint8_t>(pt.number_of_returns), static_cast<std::uint8_t>(7));
    p.legacy_classification = pt.classification < 32 ? pt.classification : 0;
    p.legacy_scan_angle_rank = i8_clamp(static_cast<std::int32_t>(std::round(pt.scan_angle * 0.006)));
    p.legacy_flags = pt.classification_flags & 0x07;
    p.legacy_point_type = static_cast<std::uint8_t>(config_.format) & 0x03;
}

void LazWriter::update_bounds(const Point14& pt) {
    double x = pt.x * config_.x_scale + config_.x_offset;
    double y = pt.y * config_.y_scale + config_.y_offset;
    double z = pt.z * config_.z_scale + config_.z_offset;

    if (x < min_x_) min_x_ = x;
    if (x > max_x_) max_x_ = x;
    if (y < min_y_) min_y_ = y;
    if (y > max_y_) max_y_ = y;
    if (z < min_z_) min_z_ = z;
    if (z > max_z_) max_z_ = z;
}

} // namespace picolaz
