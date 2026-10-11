// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Reads the standard LAZ 1.4 container written by LASzip/LAZperf (and by
// LazWriter above): LASzip VLR with items POINT14 v3 [+RGB14|RGBNIR14 v3],
// u64 chunk-table offset at the start of the point data, chunks of
// [raw first point][u32 count][u32 per-layer sizes][layer bytes], and a
// delta-coded chunk table.

#include "picolaz/laz_reader.hpp"
#include <cstring>
#include <cstdio>

namespace picolaz {

namespace {

std::uint32_t rd_u32(const std::uint8_t* p) {
  std::uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
std::uint64_t rd_u64(const std::uint8_t* p) {
  std::uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}

}

LazReader::LazReader(const std::string& path) {
    file_.open(path, std::ios::binary);
    if (!file_.is_open()) throw LazError("cannot open file for reading: " + path);

    read_header();
    find_laszip_vlr();
    read_chunk_table();
}

LazReader::~LazReader() {
    if (!closed_) {
        try { close(); } catch (...) {}
    }
}

void LazReader::read_header() {
    file_.read(reinterpret_cast<char*>(&header_), sizeof(LasHeader));
    if (!file_) throw LazError("failed to read LAS header");

    if (std::memcmp(header_.file_signature, "LASF", 4) != 0)
        throw LazError("not a LAS file");

    if (header_.version_major != 1 || header_.version_minor != 4)
        throw LazError("only LAS 1.4 supported");

    const std::uint8_t fmt = static_cast<std::uint8_t>(
        header_.point_data_record_format & 0x3F);
    header_.point_data_record_format = fmt;
    if (fmt < 6 || fmt > 8)
        throw LazError("unsupported point format " + std::to_string(fmt));
    format_ = static_cast<PointFormat>(fmt);

    if (header_.number_of_point_records == 0)
        header_.number_of_point_records = header_.legacy_number_of_point_records;
}

void LazReader::find_laszip_vlr() {
    file_.seekg(header_.header_size);
    bool found = false;

    for (std::uint32_t i = 0; i < header_.number_of_vlrs; ++i) {
        std::uint8_t vlr_head[54];
        file_.read(reinterpret_cast<char*>(vlr_head), 54);
        if (!file_) throw LazError("failed to read VLR header");
        const std::uint16_t record_id =
            static_cast<std::uint16_t>(vlr_head[18] | (vlr_head[19] << 8));
        const std::uint32_t payload_len =
            static_cast<std::uint32_t>(vlr_head[20] | (vlr_head[21] << 8));

        char uid[17] = {};
        std::memcpy(uid, vlr_head + 2, 16);

        if (!found && record_id == 22204 && std::string(uid).find("laszip") == 0) {
            std::vector<std::uint8_t> payload(payload_len);
            file_.read(reinterpret_cast<char*>(payload.data()), payload_len);
            if (!file_) throw LazError("failed to read LASzip VLR");

            const std::uint16_t compressor = static_cast<std::uint16_t>(
                payload[0] | (payload[1] << 8));
            if (compressor != 3)
                throw LazError("unsupported LASzip compressor " + std::to_string(compressor));

            chunk_size_ = rd_u32(payload.data() + 12);
            const std::uint16_t num_items = static_cast<std::uint16_t>(
                payload[32] | (payload[33] << 8));

            for (std::uint16_t it = 0; it < num_items; ++it) {
                const std::uint16_t type = static_cast<std::uint16_t>(
                    payload[34 + it * 6] | (payload[35 + it * 6] << 8));
                const std::uint16_t version = static_cast<std::uint16_t>(
                    payload[38 + it * 6] | (payload[39 + it * 6] << 8));
                if (type == v3::kItemPoint14) {
                    if (version != 3) throw LazError("unsupported POINT14 item version");
                } else if (type == v3::kItemRgb14) {
                    if (version != 3) throw LazError("unsupported RGB14 item version");
                    rgb_layer_ = true;
                } else if (type == v3::kItemRgbNir14) {
                    if (version != 3) throw LazError("unsupported RGBNIR14 item version");
                    rgb_layer_ = true;
                    nir_layer_ = true;
                } else {
                    throw LazError("unsupported LASzip item type " + std::to_string(type));
                }
            }
            found = true;
        } else {
            file_.seekg(static_cast<std::streamoff>(payload_len), std::ios::cur);
        }
    }

    if (!found) throw LazError("no LASzip VLR found");

    if (nir_layer_) rgbnir_decomp_ = std::make_unique<v3::RgbNir14DecompressorV3>();
    else if (rgb_layer_) rgb_decomp_ = std::make_unique<v3::Rgb14DecompressorV3>();
}

void LazReader::read_chunk_table() {
    std::uint8_t ptr_bytes[8];
    file_.seekg(static_cast<std::streamoff>(header_.offset_to_point_data));
    file_.read(reinterpret_cast<char*>(ptr_bytes), 8);
    if (!file_) throw LazError("failed to read chunk table position");
    const std::int64_t table_position = static_cast<std::int64_t>(rd_u64(ptr_bytes));

    file_.seekg(table_position);
    std::uint8_t head[8];
    file_.read(reinterpret_cast<char*>(head), 8);
    if (!file_) throw LazError("failed to read chunk table header");
    const std::uint32_t version = rd_u32(head);
    const std::uint32_t n = rd_u32(head + 4);
    if (version != 0) throw LazError("unsupported chunk table version");

    if (n == 0) return;

    file_.seekg(0, std::ios::end);
    const std::uint64_t file_size = static_cast<std::uint64_t>(file_.tellg());
    const std::uint64_t coded_len = file_size - table_position - 8;
    std::vector<std::uint8_t> coded(static_cast<std::size_t>(coded_len));
    file_.seekg(table_position + 8);
    file_.read(reinterpret_cast<char*>(coded.data()), coded.size());
    if (!file_) throw LazError("failed to read chunk table");

    ByteStreamIn in;
    in.init(coded.data(), static_cast<std::uint32_t>(coded.size()));
    ArithmeticDecoder dec;
    dec.init(&in);
    IntegerCompressor ic(&dec, 32, 2);
    ic.initDecompressor();

    chunk_offsets_.push_back(header_.offset_to_point_data + 8);
    std::uint64_t total = header_.offset_to_point_data + 8;
    std::int32_t prev = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::int32_t size = ic.decompress(prev, 1);
        prev = size;
        total += static_cast<std::uint64_t>(size);
        chunk_offsets_.push_back(total);
        chunk_sizes_.push_back(static_cast<std::uint64_t>(size));
    }
}

bool LazReader::start_next_chunk() {
    if (current_chunk_ >= chunk_sizes_.size()) return false;

    const std::uint64_t size = chunk_sizes_[current_chunk_];
    chunk_data_.resize(static_cast<std::size_t>(size));
    file_.seekg(static_cast<std::streamoff>(chunk_offsets_[current_chunk_]));
    file_.read(reinterpret_cast<char*>(chunk_data_.data()), size);
    if (!file_) throw LazError("failed to read chunk");

    const int record_len = point_record_length(format_);
    chunk_pos_ = record_len;

    const std::uint8_t* first = chunk_data_.data();
    std::uint32_t count = 0;
    std::memcpy(&count, chunk_data_.data() + chunk_pos_, 4);
    chunk_pos_ += 4;
    points_in_chunk_ = count;
    points_read_in_chunk_ = 0;

    std::uint32_t p14_sizes[v3::kPoint14Layers];
    for (int i = 0; i < v3::kPoint14Layers; ++i) {
        std::memcpy(&p14_sizes[i], chunk_data_.data() + chunk_pos_, 4);
        chunk_pos_ += 4;
    }
    std::uint32_t rgb_sizes[2] = {0, 0};
    int rgb_count = 0;
    if (rgbnir_decomp_) rgb_count = v3::RgbNir14DecompressorV3::kLayers;
    else if (rgb_decomp_) rgb_count = v3::Rgb14DecompressorV3::kLayers;
    for (int i = 0; i < rgb_count; ++i) {
        std::memcpy(&rgb_sizes[i], chunk_data_.data() + chunk_pos_, 4);
        chunk_pos_ += 4;
    }

    std::uint32_t p14_layer_bytes = 0;
    for (int i = 0; i < v3::kPoint14Layers; ++i) p14_layer_bytes += p14_sizes[i];

    const std::uint32_t context = (first[15] >> 4) & 0x03;
    point_decomp_.init(p14_sizes, chunk_data_.data() + chunk_pos_,
                       static_cast<std::uint32_t>(size - chunk_pos_), first, context);
    if (rgbnir_decomp_)
        rgbnir_decomp_->init(rgb_sizes, chunk_data_.data() + chunk_pos_ + p14_layer_bytes, 0,
                             first + 30, context);
    else if (rgb_decomp_)
        rgb_decomp_->init(rgb_sizes, chunk_data_.data() + chunk_pos_ + p14_layer_bytes, 0,
                          first + 30, context);

    std::memcpy(first_record_, first, 38);
    first_pending_ = true;
    ++current_chunk_;
    return true;
}

void LazReader::record_to_point(const std::uint8_t* rec, Point14& pt) {
    std::memcpy(&pt.x, rec + 0, 4);
    std::memcpy(&pt.y, rec + 4, 4);
    std::memcpy(&pt.z, rec + 8, 4);
    std::memcpy(&pt.intensity, rec + 12, 2);
    pt.return_number = rec[14] & 0x0F;
    pt.number_of_returns = (rec[14] >> 4) & 0x0F;
    pt.classification_flags = rec[15] & 0x0F;
    pt.scanner_channel = (rec[15] >> 4) & 0x03;
    pt.scan_direction_flag = (rec[15] >> 6) & 0x01;
    pt.edge_of_flight_line = (rec[15] >> 7) & 0x01;
    pt.classification = rec[16];
    pt.user_data = rec[17];
    std::memcpy(&pt.scan_angle, rec + 18, 2);
    std::memcpy(&pt.point_source_id, rec + 20, 2);
    std::memcpy(&pt.gps_time, rec + 22, 8);
    std::memcpy(&pt.red, rec + 30, 2);
    std::memcpy(&pt.green, rec + 32, 2);
    std::memcpy(&pt.blue, rec + 34, 2);
    std::memcpy(&pt.nir, rec + 36, 2);
}

bool LazReader::read_point(Point14& pt) {
    if (points_read_in_chunk_ == points_in_chunk_) {
        if (!start_next_chunk()) return false;
    }

    if (total_points_read_ >= point_count()) return false;

    if (first_pending_) {
        first_pending_ = false;
        record_to_point(first_record_, pt);
    } else {
        std::uint8_t rec[38];
        std::uint32_t context = 0;
        point_decomp_.read(rec, context);
        if (rgbnir_decomp_) rgbnir_decomp_->read(rec + 30, context);
        else if (rgb_decomp_) rgb_decomp_->read(rec + 30, context);
        record_to_point(rec, pt);
    }

    ++points_read_in_chunk_;
    ++total_points_read_;
    return true;
}

std::vector<Point14> LazReader::read_points(std::uint64_t count) {
    std::vector<Point14> pts;
    pts.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        Point14 pt;
        if (!read_point(pt)) break;
        pts.push_back(pt);
    }
    return pts;
}

void LazReader::close() {
    if (closed_) return;
    closed_ = true;
    if (file_.is_open()) file_.close();
}

}
