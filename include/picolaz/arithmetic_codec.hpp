#pragma once

// Arithmetic encoder/decoder based on Amir Said's FastAC,
// adapted by Martin Isenburg for LASzip. C++20 reimplementation.

#include "types.hpp"
#include "streams.hpp"
#include <memory>

namespace picolaz {

// ─── Constants ───────────────────────────────────────────────────────
inline constexpr std::uint32_t AC_BUFFER_SIZE  = 4096;
inline constexpr std::uint32_t AC__MinLength   = 0x01000000U;
inline constexpr std::uint32_t AC__MaxLength   = 0xFFFFFFFFU;

inline constexpr std::uint32_t BM__LengthShift = 13;
inline constexpr std::uint32_t BM__MaxCount    = 1U << BM__LengthShift;

inline constexpr std::uint32_t DM__LengthShift = 15;
inline constexpr std::uint32_t DM__MaxCount    = 1U << DM__LengthShift;

// ─── ArithmeticBitModel ──────────────────────────────────────────────
struct ArithmeticBitModel {
    std::uint32_t update_cycle{4};
    std::uint32_t bits_until_update{4};
    std::uint32_t bit_0_prob{1U << (BM__LengthShift - 1)};
    std::uint32_t bit_0_count{1};
    std::uint32_t bit_count{2};

    void init();
    void update();
};

// ─── ArithmeticModel ─────────────────────────────────────────────────
class ArithmeticModel {
public:
    ArithmeticModel() = default;
    ~ArithmeticModel();

    ArithmeticModel(const ArithmeticModel&) = delete;
    ArithmeticModel& operator=(const ArithmeticModel&) = delete;
    ArithmeticModel(ArithmeticModel&& o) noexcept;
    ArithmeticModel& operator=(ArithmeticModel&& o) noexcept;

    bool init(std::uint32_t num_symbols, bool compress, const std::uint32_t* init_table = nullptr);
    void update();

    std::uint32_t  symbols{0};
    bool compress{false};
    std::uint32_t* distribution{nullptr};
    std::uint32_t* symbol_count{nullptr};
    std::uint32_t* decoder_table{nullptr};
    std::uint32_t  total_count{0};
    std::uint32_t  update_cycle{0};
    std::uint32_t  symbols_until_update{0};
    std::uint32_t  last_symbol{0};
    std::uint32_t  table_size{0};
    std::uint32_t  table_shift{0};

private:
    void free_data();
};

// ─── ArithmeticEncoder ───────────────────────────────────────────────
class ArithmeticEncoder {
public:
    ArithmeticEncoder() = default;
    ~ArithmeticEncoder() = default;

    void init(ByteStreamOut* out);
    void done();

    void encodeBit(ArithmeticBitModel& m, std::uint32_t sym);
    void encodeSymbol(ArithmeticModel& m, std::uint32_t sym);

    void writeBit(std::uint32_t sym);
    void writeBits(std::uint32_t bits, std::uint32_t sym);
    void writeByte(std::uint8_t sym);
    void writeShort(std::uint16_t sym);
    void writeInt(std::uint32_t sym);
    void writeInt64(std::uint64_t sym);

private:
    void propagate_carry();
    void renorm_enc_interval();

    ByteStreamOut* out_{nullptr};
    std::uint8_t   buf_[2 * AC_BUFFER_SIZE]{};
    std::uint32_t  base_{0};
    std::uint32_t  length_{AC__MaxLength};
    std::uint32_t  buf_pos_{0};
};

// ─── ArithmeticDecoder ───────────────────────────────────────────────
class ArithmeticDecoder {
public:
    ArithmeticDecoder() = default;
    ~ArithmeticDecoder() = default;

    void init(ByteStreamIn* in);

    std::uint32_t decodeBit(ArithmeticBitModel& m);
    std::uint32_t decodeSymbol(ArithmeticModel& m);

    std::uint32_t readBit();
    std::uint32_t readBits(std::uint32_t bits);
    std::uint8_t  readByte();
    std::uint16_t readShort();
    std::uint32_t readInt();
    std::uint64_t readInt64();

private:
    void renorm_dec_interval();

    ByteStreamIn* in_{nullptr};
    std::uint32_t value_{0};
    std::uint32_t length_{0};
};

} // namespace picolaz
