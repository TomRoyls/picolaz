#include "picolaz/arithmetic_codec.hpp"
#include <algorithm>
#include <cstring>

namespace picolaz {

// ─── ArithmeticBitModel ──────────────────────────────────────────────

void ArithmeticBitModel::init() {
    bit_0_count = 1;
    bit_count = 2;
    bit_0_prob = 1U << (BM__LengthShift - 1);
    update_cycle = bits_until_update = 4;
}

void ArithmeticBitModel::update() {
    if ((bit_count += update_cycle) > BM__MaxCount) {
        bit_count = (bit_count + 1) >> 1;
        bit_0_count = (bit_0_count + 1) >> 1;
        if (bit_0_count == bit_count) ++bit_count;
    }

    std::uint32_t scale = 0x80000000U / bit_count;
    bit_0_prob = (bit_0_count * scale) >> (31 - BM__LengthShift);

    update_cycle = (5 * update_cycle) >> 2;
    if (update_cycle > 64) update_cycle = 64;
    bits_until_update = update_cycle;
}

// ─── ArithmeticModel ─────────────────────────────────────────────────

ArithmeticModel::~ArithmeticModel() {
    free_data();
}

ArithmeticModel::ArithmeticModel(ArithmeticModel&& o) noexcept
    : symbols(o.symbols), compress(o.compress),
      distribution(o.distribution), symbol_count(o.symbol_count),
      decoder_table(o.decoder_table), total_count(o.total_count),
      update_cycle(o.update_cycle), symbols_until_update(o.symbols_until_update),
      last_symbol(o.last_symbol), table_size(o.table_size), table_shift(o.table_shift) {
    o.distribution = nullptr;
    o.symbol_count = nullptr;
    o.decoder_table = nullptr;
}

ArithmeticModel& ArithmeticModel::operator=(ArithmeticModel&& o) noexcept {
    if (this != &o) {
        free_data();
        symbols = o.symbols;
        compress = o.compress;
        distribution = o.distribution;
        symbol_count = o.symbol_count;
        decoder_table = o.decoder_table;
        total_count = o.total_count;
        update_cycle = o.update_cycle;
        symbols_until_update = o.symbols_until_update;
        last_symbol = o.last_symbol;
        table_size = o.table_size;
        table_shift = o.table_shift;
        o.distribution = nullptr;
        o.symbol_count = nullptr;
        o.decoder_table = nullptr;
    }
    return *this;
}

void ArithmeticModel::free_data() {
    delete[] distribution;
    distribution = nullptr;
    symbol_count = nullptr;
    decoder_table = nullptr;
}

bool ArithmeticModel::init(std::uint32_t num_symbols, bool is_compress, const std::uint32_t* init_table) {
    free_data();
    symbols = num_symbols;
    compress = is_compress;
    last_symbol = symbols - 1;

    if (symbols < 2 || symbols > (1U << 11)) return false;

    std::uint32_t table_elements = 0;
    if (!compress && symbols > 16) {
        std::uint32_t table_bits = 3;
        while (symbols > (1U << (table_bits + 2))) ++table_bits;
        table_size = 1U << table_bits;
        table_shift = DM__LengthShift - table_bits;
        table_elements = table_size + 2;
    } else {
        table_size = 0;
        table_shift = 0;
    }

    std::uint32_t alloc_size = 2 * symbols + table_elements;
    distribution = new std::uint32_t[alloc_size];
    symbol_count = distribution + symbols;
    if (table_elements) {
        decoder_table = symbol_count + symbols;
    } else {
        decoder_table = nullptr;
    }

    total_count = 0;
    update_cycle = symbols;
    if (init_table) {
        for (std::uint32_t i = 0; i < symbols; ++i) symbol_count[i] = init_table[i];
    } else {
        for (std::uint32_t i = 0; i < symbols; ++i) symbol_count[i] = 1;
    }

    update();
    symbols_until_update = update_cycle = (symbols + 6) >> 1;

    return true;
}

void ArithmeticModel::update() {
    // halve counts when a threshold is reached
    if ((total_count += update_cycle) > DM__MaxCount) {
        total_count = 0;
        for (std::uint32_t n = 0; n < symbols; ++n) {
            total_count += (symbol_count[n] = (symbol_count[n] + 1) >> 1);
        }
    }

    // compute cumulative distribution, decoder table
    std::uint32_t k, sum = 0, s = 0;
    std::uint32_t scale = 0x80000000U / total_count;

    if (compress || (table_size == 0)) {
        for (k = 0; k < symbols; ++k) {
            distribution[k] = (scale * sum) >> (31 - DM__LengthShift);
            sum += symbol_count[k];
        }
    } else {
        for (k = 0; k < symbols; ++k) {
            distribution[k] = (scale * sum) >> (31 - DM__LengthShift);
            sum += symbol_count[k];
            std::uint32_t w = distribution[k] >> table_shift;
            while (s < w) decoder_table[++s] = k - 1;
        }
        decoder_table[0] = 0;
        while (s <= table_size) decoder_table[++s] = symbols - 1;
    }

    // set frequency of model updates
    update_cycle = (5 * update_cycle) >> 2;
    std::uint32_t max_cycle = (symbols + 6) << 3;
    if (update_cycle > max_cycle) update_cycle = max_cycle;
    symbols_until_update = update_cycle;
}

// ─── ArithmeticEncoder ───────────────────────────────────────────────

void ArithmeticEncoder::init(ByteStreamOut* out) {
    out_ = out;
    base_ = 0;
    length_ = AC__MaxLength;
    buf_pos_ = 0;
}

void ArithmeticEncoder::done() {
    std::uint32_t init_base = base_;
    bool another_byte = true;

    if (length_ > 2 * AC__MinLength) {
        base_ += AC__MinLength;
        length_ = AC__MinLength >> 1;
    } else {
        base_ += AC__MinLength >> 1;
        length_ = AC__MinLength >> 9;
        another_byte = false;
    }

    if (init_base > base_) propagate_carry();

    renorm_enc_interval();

    std::uint32_t buf_bytes = buf_pos_;
    if (buf_bytes) out_->put_bytes(buf_, buf_bytes);

    std::uint8_t zero = 0;
    out_->put_byte(zero);
    out_->put_byte(zero);
    if (another_byte) out_->put_byte(zero);
}

void ArithmeticEncoder::encodeBit(ArithmeticBitModel& m, std::uint32_t sym) {
    std::uint32_t x = m.bit_0_prob * (length_ >> BM__LengthShift);

    if (sym == 0) {
        length_ = x;
        ++m.bit_0_count;
    } else {
        std::uint32_t init_base = base_;
        base_ += x;
        length_ -= x;
        if (init_base > base_) propagate_carry();
    }

    if (length_ < AC__MinLength) renorm_enc_interval();
    if (--m.bits_until_update == 0) m.update();
}

void ArithmeticEncoder::encodeSymbol(ArithmeticModel& m, std::uint32_t sym) {
    std::uint32_t x;
    std::uint32_t init_base = base_;

    if (sym == m.last_symbol) {
        x = m.distribution[sym] * (length_ >> DM__LengthShift);
        base_ += x;
        length_ -= x;
    } else {
        x = m.distribution[sym] * (length_ >>= DM__LengthShift);
        base_ += x;
        length_ = m.distribution[sym + 1] * length_ - x;
    }

    if (init_base > base_) propagate_carry();
    if (length_ < AC__MinLength) renorm_enc_interval();

    ++m.symbol_count[sym];
    if (--m.symbols_until_update == 0) m.update();
}

void ArithmeticEncoder::writeBit(std::uint32_t sym) {
    std::uint32_t init_base = base_;
    base_ += sym * (length_ >>= 1);
    if (init_base > base_) propagate_carry();
    if (length_ < AC__MinLength) renorm_enc_interval();
}

void ArithmeticEncoder::writeBits(std::uint32_t bits, std::uint32_t sym) {
    if (bits > 19) {
        writeShort(static_cast<std::uint16_t>(sym & 0xFFFF));
        sym >>= 16;
        bits -= 16;
    }

    std::uint32_t init_base = base_;
    base_ += sym * (length_ >>= bits);
    if (init_base > base_) propagate_carry();
    if (length_ < AC__MinLength) renorm_enc_interval();
}

void ArithmeticEncoder::writeByte(std::uint8_t sym) {
    std::uint32_t init_base = base_;
    base_ += static_cast<std::uint32_t>(sym) * (length_ >>= 8);
    if (init_base > base_) propagate_carry();
    if (length_ < AC__MinLength) renorm_enc_interval();
}

void ArithmeticEncoder::writeShort(std::uint16_t sym) {
    std::uint32_t init_base = base_;
    base_ += static_cast<std::uint32_t>(sym) * (length_ >>= 16);
    if (init_base > base_) propagate_carry();
    if (length_ < AC__MinLength) renorm_enc_interval();
}

void ArithmeticEncoder::writeInt(std::uint32_t sym) {
    writeShort(static_cast<std::uint16_t>(sym & 0xFFFF));
    writeShort(static_cast<std::uint16_t>(sym >> 16));
}

void ArithmeticEncoder::writeInt64(std::uint64_t sym) {
    writeInt(static_cast<std::uint32_t>(sym & 0xFFFFFFFFU));
    writeInt(static_cast<std::uint32_t>(sym >> 32));
}

void ArithmeticEncoder::propagate_carry() {
    std::uint32_t p = buf_pos_ - 1;
    while (buf_[p] == 0xFF) {
        buf_[p] = 0;
        if (p == 0) {
            // Wrapped around - flush and reset
            out_->put_bytes(buf_, buf_pos_);
            buf_pos_ = 0;
            return;
        }
        --p;
    }
    ++buf_[p];
}

void ArithmeticEncoder::renorm_enc_interval() {
    do {
        buf_[buf_pos_++] = static_cast<std::uint8_t>(base_ >> 24);
        base_ <<= 8;
        length_ <<= 8;

        if (buf_pos_ >= 2 * AC_BUFFER_SIZE) {
            out_->put_bytes(buf_, AC_BUFFER_SIZE);
            std::memmove(buf_, buf_ + AC_BUFFER_SIZE, AC_BUFFER_SIZE);
            buf_pos_ -= AC_BUFFER_SIZE;
        }
    } while (length_ < AC__MinLength);
}

// ─── ArithmeticDecoder ───────────────────────────────────────────────

void ArithmeticDecoder::init(ByteStreamIn* in) {
    in_ = in;
    length_ = AC__MaxLength;
    // Read 4 initial bytes big-endian into value
    value_ = (static_cast<std::uint32_t>(in_->get_byte()) << 24) |
             (static_cast<std::uint32_t>(in_->get_byte()) << 16) |
             (static_cast<std::uint32_t>(in_->get_byte()) << 8) |
             static_cast<std::uint32_t>(in_->get_byte());
}

std::uint32_t ArithmeticDecoder::decodeBit(ArithmeticBitModel& m) {
    std::uint32_t x = m.bit_0_prob * (length_ >> BM__LengthShift);
    std::uint32_t sym = (value_ >= x);

    if (sym == 0) {
        length_ = x;
        ++m.bit_0_count;
    } else {
        value_ -= x;
        length_ -= x;
    }

    if (length_ < AC__MinLength) renorm_dec_interval();
    if (--m.bits_until_update == 0) m.update();

    return sym;
}

std::uint32_t ArithmeticDecoder::decodeSymbol(ArithmeticModel& m) {
    std::uint32_t n, sym, x, y = length_;

    if (m.decoder_table) {
        std::uint32_t dv = value_ / (length_ >>= DM__LengthShift);
        std::uint32_t t = dv >> m.table_shift;

        sym = m.decoder_table[t];
        n = m.decoder_table[t + 1] + 1;

        while (n > sym + 1) {
            std::uint32_t k = (sym + n) >> 1;
            if (m.distribution[k] > dv) n = k; else sym = k;
        }

        x = m.distribution[sym] * length_;
        if (sym != m.last_symbol) {
            y = m.distribution[sym + 1] * length_;
        }
    } else {
        x = sym = 0;
        length_ >>= DM__LengthShift;
        std::uint32_t k = (n = m.symbols) >> 1;

        do {
            std::uint32_t z = length_ * m.distribution[k];
            if (z > value_) { n = k; y = z; }
            else { sym = k; x = z; }
        } while ((k = (sym + n) >> 1) != sym);
    }

    value_ -= x;
    length_ = y - x;

    if (length_ < AC__MinLength) renorm_dec_interval();

    ++m.symbol_count[sym];
    if (--m.symbols_until_update == 0) m.update();

    return sym;
}

std::uint32_t ArithmeticDecoder::readBit() {
    std::uint32_t sym = value_ / (length_ >>= 1);
    value_ -= length_ * sym;
    if (length_ < AC__MinLength) renorm_dec_interval();
    return sym;
}

std::uint32_t ArithmeticDecoder::readBits(std::uint32_t bits) {
    if (bits > 19) {
        std::uint32_t lo = readShort();
        std::uint32_t hi = readBits(bits - 16);
        return lo | (hi << 16);
    }

    std::uint32_t sym = value_ / (length_ >>= bits);
    value_ -= length_ * sym;
    if (length_ < AC__MinLength) renorm_dec_interval();
    return sym;
}

std::uint8_t ArithmeticDecoder::readByte() {
    std::uint32_t sym = value_ / (length_ >>= 8);
    value_ -= length_ * sym;
    if (length_ < AC__MinLength) renorm_dec_interval();
    return static_cast<std::uint8_t>(sym);
}

std::uint16_t ArithmeticDecoder::readShort() {
    std::uint32_t sym = value_ / (length_ >>= 16);
    value_ -= length_ * sym;
    if (length_ < AC__MinLength) renorm_dec_interval();
    return static_cast<std::uint16_t>(sym);
}

std::uint32_t ArithmeticDecoder::readInt() {
    std::uint32_t lo = readShort();
    std::uint32_t hi = readShort();
    return lo | (static_cast<std::uint32_t>(hi) << 16);
}

std::uint64_t ArithmeticDecoder::readInt64() {
    std::uint64_t lo = readInt();
    std::uint64_t hi = readInt();
    return lo | (hi << 32);
}

void ArithmeticDecoder::renorm_dec_interval() {
    do {
        value_ = (value_ << 8) | static_cast<std::uint32_t>(in_->get_byte());
        length_ <<= 8;
    } while (length_ < AC__MinLength);
}

} // namespace picolaz
