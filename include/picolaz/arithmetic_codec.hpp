#pragma once

// Arithmetic encoder/decoder based on Amir Said's FastAC,
// adapted by Martin Isenburg for LASzip. C++20 reimplementation.

#include "types.hpp"
#include "streams.hpp"
#include <memory>

namespace picolaz {

// ─── Constants ───────────────────────────────────────────────────────
inline constexpr u32 AC_BUFFER_SIZE  = 4096;
inline constexpr u32 AC__MinLength   = 0x01000000U;
inline constexpr u32 AC__MaxLength   = 0xFFFFFFFFU;

inline constexpr u32 BM__LengthShift = 13;
inline constexpr u32 BM__MaxCount    = 1U << BM__LengthShift;

inline constexpr u32 DM__LengthShift = 15;
inline constexpr u32 DM__MaxCount    = 1U << DM__LengthShift;

// ─── ArithmeticBitModel ──────────────────────────────────────────────
struct ArithmeticBitModel {
    u32 update_cycle{4};
    u32 bits_until_update{4};
    u32 bit_0_prob{1U << (BM__LengthShift - 1)};
    u32 bit_0_count{1};
    u32 bit_count{2};

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

    bool init(u32 num_symbols, bool compress, const u32* init_table = nullptr);
    void update();

    u32  symbols{0};
    bool compress{false};
    u32* distribution{nullptr};
    u32* symbol_count{nullptr};
    u32* decoder_table{nullptr};
    u32  total_count{0};
    u32  update_cycle{0};
    u32  symbols_until_update{0};
    u32  last_symbol{0};
    u32  table_size{0};
    u32  table_shift{0};

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

    void encodeBit(ArithmeticBitModel& m, u32 sym);
    void encodeSymbol(ArithmeticModel& m, u32 sym);

    void writeBit(u32 sym);
    void writeBits(u32 bits, u32 sym);
    void writeByte(u8 sym);
    void writeShort(u16 sym);
    void writeInt(u32 sym);
    void writeInt64(u64 sym);

private:
    void propagate_carry();
    void renorm_enc_interval();

    ByteStreamOut* out_{nullptr};
    u8   buf_[2 * AC_BUFFER_SIZE]{};
    u32  base_{0};
    u32  length_{AC__MaxLength};
    u32  buf_pos_{0};
};

// ─── ArithmeticDecoder ───────────────────────────────────────────────
class ArithmeticDecoder {
public:
    ArithmeticDecoder() = default;
    ~ArithmeticDecoder() = default;

    void init(ByteStreamIn* in);

    u32 decodeBit(ArithmeticBitModel& m);
    u32 decodeSymbol(ArithmeticModel& m);

    u32 readBit();
    u32 readBits(u32 bits);
    u8  readByte();
    u16 readShort();
    u32 readInt();
    u64 readInt64();

private:
    void renorm_dec_interval();

    ByteStreamIn* in_{nullptr};
    u32 value_{0};
    u32 length_{0};
};

} // namespace picolaz
