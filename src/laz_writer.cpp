// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Writes the standard LAZ 1.4 container as produced by LASzip/LAZperf:
// LASzip VLR (compressor 3, items POINT14 v3 [+RGB14|RGBNIR14 v3]), a u64
// chunk-table offset at the start of the point data, chunks of
// [raw first point][u32 count][u32 per-layer sizes][layer bytes], and a
// chunk table [u32 0][u32 n][delta-coded sizes] at the recorded offset.

#include "picolaz/laz_writer.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace picolaz {

namespace {

using v3::kPoint14Layers;

void put_u16(ByteStreamOut& s, std::uint16_t v) {
  s.put_bytes(reinterpret_cast<const std::uint8_t*>(&v), 2);
}
void put_u32(ByteStreamOut& s, std::uint32_t v) {
  s.put_bytes(reinterpret_cast<const std::uint8_t*>(&v), 4);
}

}

LazWriter::LazWriter(const std::string& path, const Config& config)
    : config_(config) {
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) throw LazError("cannot open file for writing: " + path);

    if (format_has_nir(config_.format)) {
        rgbnir_comp_ = std::make_unique<v3::RgbNir14CompressorV3>();
    } else if (format_has_rgb(config_.format)) {
        rgb_comp_ = std::make_unique<v3::Rgb14CompressorV3>();
    }

    write_header();
    write_laszip_vlr();

    std::uint8_t placeholder[8] = {0};
    file_.write(reinterpret_cast<const char*>(placeholder), 8);
}

LazWriter::~LazWriter() {
    if (!closed_) {
        try { close(); } catch (...) {}
    }
}

void LazWriter::write_header() {
    header_ = LasHeader{};
    header_.file_signature[0] = 'L';
    header_.file_signature[1] = 'A';
    header_.file_signature[2] = 'S';
    header_.file_signature[3] = 'F';
    header_.version_major = 1;
    header_.version_minor = 4;

    const char* gen_sw = "picolaz";
    std::strncpy(header_.generating_software, gen_sw, 31);

    header_.header_size = 375;
    header_.offset_to_point_data = 375 + 54 + (34 + 6 * static_cast<int>(!!rgb_comp_ + !!rgbnir_comp_ + 1));
    header_.number_of_vlrs = 1;
    header_.point_data_record_format = static_cast<std::uint8_t>(config_.format) | 0x80;
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
    std::uint8_t reserved[2] = {0};
    char user_id[16] = {};
    std::strncpy(user_id, "laszip encoded", 15);
    std::uint16_t record_id = 22204;
    std::uint16_t num_items = 1 + (rgb_comp_ || rgbnir_comp_ ? 1 : 0);
    std::uint32_t payload_size = 34 + 6 * num_items;

    ByteStreamOut vlr;
    vlr.put_bytes(reserved, 2);
    vlr.put_bytes(reinterpret_cast<const std::uint8_t*>(user_id), 16);
    vlr.put_bytes(reinterpret_cast<const std::uint8_t*>(&record_id), 2);
    put_u16(vlr, static_cast<std::uint16_t>(payload_size));
    std::uint8_t desc[32] = {};
    vlr.put_bytes(desc, 32);

    ByteStreamOut payload;
    put_u16(payload, 3);
    put_u16(payload, 0);
    std::uint8_t ver[2] = {3, 4};
    payload.put_bytes(ver, 2);
    put_u16(payload, 0);
    put_u32(payload, 0);
    put_u32(payload, config_.chunk_size);
    std::int64_t m1 = -1;
    payload.put_bytes(reinterpret_cast<const std::uint8_t*>(&m1), 8);
    payload.put_bytes(reinterpret_cast<const std::uint8_t*>(&m1), 8);
    put_u16(payload, num_items);
    put_u16(payload, v3::kItemPoint14);
    put_u16(payload, 30);
    put_u16(payload, 3);
    if (rgbnir_comp_) {
        put_u16(payload, v3::kItemRgbNir14);
        put_u16(payload, 8);
        put_u16(payload, 3);
    } else if (rgb_comp_) {
        put_u16(payload, v3::kItemRgb14);
        put_u16(payload, 6);
        put_u16(payload, 3);
    }

    file_.write(reinterpret_cast<const char*>(vlr.data()), vlr.size());
    file_.write(reinterpret_cast<const char*>(payload.data()), payload.size());
}

void LazWriter::point_to_record(const Point14& pt, std::uint8_t* rec) {
    std::memset(rec, 0, 38);
    std::memcpy(rec + 0, &pt.x, 4);
    std::memcpy(rec + 4, &pt.y, 4);
    std::memcpy(rec + 8, &pt.z, 4);
    std::memcpy(rec + 12, &pt.intensity, 2);
    rec[14] = static_cast<std::uint8_t>((pt.return_number & 0x0F) |
                                        ((pt.number_of_returns & 0x0F) << 4));
    rec[15] = static_cast<std::uint8_t>((pt.classification_flags & 0x0F) |
                                        ((pt.scanner_channel & 0x03) << 4) |
                                        ((pt.scan_direction_flag & 0x01) << 6) |
                                        ((pt.edge_of_flight_line & 0x01) << 7));
    rec[16] = pt.classification;
    rec[17] = pt.user_data;
    std::memcpy(rec + 18, &pt.scan_angle, 2);
    std::memcpy(rec + 20, &pt.point_source_id, 2);
    std::memcpy(rec + 22, &pt.gps_time, 8);
    std::memcpy(rec + 30, &pt.red, 2);
    std::memcpy(rec + 32, &pt.green, 2);
    std::memcpy(rec + 34, &pt.blue, 2);
    std::memcpy(rec + 36, &pt.nir, 2);
}

void LazWriter::write_point(const Point14& pt) {
    update_bounds(pt);

    std::uint8_t rec[38];
    point_to_record(pt, rec);

    if (!chunk_started_) {
        start_chunk(pt);
        return;
    }

    std::uint32_t context = (rec[15] >> 4) & 0x03;
    point_comp_.write(rec, context);
    if (rgbnir_comp_) rgbnir_comp_->write(rec + 30, context);
    else if (rgb_comp_) rgb_comp_->write(rec + 30, context);

    ++points_in_chunk_;
    ++total_points_;

    if (points_in_chunk_ >= config_.chunk_size) {
        finish_chunk();
    }
}

void LazWriter::write_points(std::span<const Point14> pts) {
    for (const auto& pt : pts) write_point(pt);
}

void LazWriter::start_chunk(const Point14& pt) {
    chunk_started_ = true;
    points_in_chunk_ = 1;
    ++total_points_;

    std::uint8_t rec[38];
    point_to_record(pt, rec);

    const int record_len = point_record_length(config_.format);
    file_.write(reinterpret_cast<const char*>(rec), record_len);

    std::uint32_t context = (rec[15] >> 4) & 0x03;
    point_comp_.init(rec, context);
    if (rgbnir_comp_) rgbnir_comp_->init(rec + 30, context);
    else if (rgb_comp_) rgb_comp_->init(rec + 30, context);
}

void LazWriter::finish_chunk() {
    std::uint32_t p14_sizes[kPoint14Layers];
    point_comp_.finish_chunk(p14_sizes);

    std::uint32_t rgb_sizes[2] = {0, 0};
    int rgb_layer_count = 0;
    if (rgbnir_comp_) {
        rgbnir_comp_->finish_chunk(rgb_sizes);
        rgb_layer_count = v3::RgbNir14CompressorV3::kLayers;
    } else if (rgb_comp_) {
        rgb_comp_->finish_chunk(rgb_sizes);
        rgb_layer_count = v3::Rgb14CompressorV3::kLayers;
    }

    chunk_out_ = ByteStreamOut();
    put_u32(chunk_out_, points_in_chunk_);
    for (int i = 0; i < kPoint14Layers; ++i) put_u32(chunk_out_, p14_sizes[i]);
    for (int i = 0; i < rgb_layer_count; ++i) put_u32(chunk_out_, rgb_sizes[i]);

    ByteStreamOut layer_bytes;
    point_comp_.write_layer_bytes(&layer_bytes);
    if (rgbnir_comp_) rgbnir_comp_->write_layer_bytes(&layer_bytes);
    else if (rgb_comp_) rgb_comp_->write_layer_bytes(&layer_bytes);
    chunk_out_.put_bytes(layer_bytes.data(), layer_bytes.size());

    file_.write(reinterpret_cast<const char*>(chunk_out_.data()), chunk_out_.size());

    chunk_total_sizes_.push_back(static_cast<std::uint64_t>(point_record_length(config_.format)) +
                                 4 + 4 * (kPoint14Layers + rgb_layer_count) +
                                 layer_bytes.size());
    chunk_started_ = false;
}

void LazWriter::write_chunk_table() {
    ByteStreamOut table;
    put_u32(table, 0);
    put_u32(table, static_cast<std::uint32_t>(chunk_total_sizes_.size()));

    if (!chunk_total_sizes_.empty()) {
        ArithmeticEncoder enc;
        enc.init(&table);
        IntegerCompressor ic(&enc, 32, 2);
        ic.initCompressor();
        std::uint32_t prev = 0;
        for (const std::uint64_t size : chunk_total_sizes_) {
            ic.compress(static_cast<std::int32_t>(prev),
                        static_cast<std::int32_t>(size), 1);
            prev = static_cast<std::uint32_t>(size);
        }
        enc.done();
    }

    std::uint64_t pos = table_position_;
    file_.seekp(static_cast<std::streamoff>(header_.offset_to_point_data));
    file_.write(reinterpret_cast<const char*>(&pos), 8);

    file_.seekp(static_cast<std::streamoff>(table_position_));
    file_.write(reinterpret_cast<const char*>(table.data()), table.size());
}

void LazWriter::close() {
    if (closed_) return;
    closed_ = true;

    if (chunk_started_) finish_chunk();

    table_position_ = static_cast<std::uint64_t>(file_.tellp());

    header_.legacy_number_of_point_records = 0;
    header_.number_of_point_records = total_points_;
    header_.max_x = max_x_;
    header_.min_x = min_x_;
    header_.max_y = max_y_;
    header_.min_y = min_y_;
    header_.max_z = max_z_;
    header_.min_z = min_z_;

    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(&header_), sizeof(LasHeader));

    if (!chunk_table_written_) {
        write_chunk_table();
        chunk_table_written_ = true;
    }

    file_.flush();
    file_.close();
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

}
