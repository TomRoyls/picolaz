#include <catch2/catch_test_macros.hpp>
#include <picolaz/arithmetic_codec.hpp>
#include <picolaz/streams.hpp>
#include <vector>
#include <cstdlib>

using namespace picolaz;

TEST_CASE("ArithmeticBitModel encode/decode single bits", "[arithmetic]") {
    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    ArithmeticBitModel bm_enc;
    bm_enc.init();

    std::vector<std::uint32_t> bits = {0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 0};
    for (auto b : bits) {
        enc.encodeBit(bm_enc, b);
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    ArithmeticBitModel bm_dec;
    bm_dec.init();

    for (auto expected : bits) {
        CHECK(dec.decodeBit(bm_dec) == expected);
    }
}

TEST_CASE("ArithmeticModel encode/decode symbol sequence", "[arithmetic]") {
    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    ArithmeticModel m_enc;
    m_enc.init(4, true);

    std::vector<std::uint32_t> symbols = {0, 1, 2, 3, 0, 1, 2, 3, 3, 2, 1, 0};
    for (auto s : symbols) {
        enc.encodeSymbol(m_enc, s);
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    ArithmeticModel m_dec;
    m_dec.init(4, false);

    for (auto expected : symbols) {
        CHECK(dec.decodeSymbol(m_dec) == expected);
    }
}

TEST_CASE("ArithmeticEncoder/Decoder raw bit operations", "[arithmetic]") {
    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    SECTION("writeBit/readBit") {
        enc.writeBit(0);
        enc.writeBit(1);
        enc.writeBit(1);
        enc.writeBit(0);
        enc.done();

        ByteStreamIn in(out.data(), out.size());
        ArithmeticDecoder dec;
        dec.init(&in);

        CHECK(dec.readBit() == 0);
        CHECK(dec.readBit() == 1);
        CHECK(dec.readBit() == 1);
        CHECK(dec.readBit() == 0);
    }

    SECTION("writeBits/readBits") {
        enc.writeBits(8, 0xAB);
        enc.writeBits(16, 0x1234);
        enc.writeBits(4, 0x0F);
        enc.done();

        ByteStreamIn in(out.data(), out.size());
        ArithmeticDecoder dec;
        dec.init(&in);

        CHECK(dec.readBits(8) == 0xAB);
        CHECK(dec.readBits(16) == 0x1234);
        CHECK(dec.readBits(4) == 0x0F);
    }
}

TEST_CASE("ArithmeticEncoder/Decoder raw byte/short/int/int64", "[arithmetic]") {
    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    enc.writeByte(0x42);
    enc.writeShort(0xBEEF);
    enc.writeInt(0xDEADBEEF);
    enc.writeInt64(0x0123456789ABCDEFULL);
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    CHECK(dec.readByte() == 0x42);
    CHECK(dec.readShort() == 0xBEEF);
    CHECK(dec.readInt() == 0xDEADBEEF);
    CHECK(dec.readInt64() == 0x0123456789ABCDEFULL);
}

TEST_CASE("ArithmeticBitModel large sequence roundtrip", "[arithmetic]") {
    constexpr int N = 2000;
    std::vector<std::uint32_t> bits(N);
    std::srand(12345);
    for (int i = 0; i < N; i++) {
        bits[i] = std::rand() % 2;
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    ArithmeticBitModel bm_enc;
    bm_enc.init();
    for (auto b : bits) {
        enc.encodeBit(bm_enc, b);
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    ArithmeticBitModel bm_dec;
    bm_dec.init();
    for (int i = 0; i < N; i++) {
        CHECK(dec.decodeBit(bm_dec) == bits[i]);
    }
}

TEST_CASE("ArithmeticModel large sequence roundtrip", "[arithmetic]") {
    constexpr int N = 5000;
    constexpr std::uint32_t NUM_SYMBOLS = 16;

    std::vector<std::uint32_t> symbols(N);
    std::srand(54321);
    for (int i = 0; i < N; i++) {
        symbols[i] = std::rand() % NUM_SYMBOLS;
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    ArithmeticModel m_enc;
    m_enc.init(NUM_SYMBOLS, true);
    for (auto s : symbols) {
        enc.encodeSymbol(m_enc, s);
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    ArithmeticModel m_dec;
    m_dec.init(NUM_SYMBOLS, false);
    for (int i = 0; i < N; i++) {
        CHECK(dec.decodeSymbol(m_dec) == symbols[i]);
    }
}

TEST_CASE("Multiple models sharing same encoder/decoder", "[arithmetic]") {
    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    ArithmeticModel m1_enc, m2_enc;
    m1_enc.init(4, true);
    m2_enc.init(8, true);

    ArithmeticBitModel bm_enc;
    bm_enc.init();

    enc.encodeSymbol(m1_enc, 2);
    enc.encodeBit(bm_enc, 1);
    enc.encodeSymbol(m2_enc, 5);
    enc.encodeSymbol(m1_enc, 0);
    enc.encodeBit(bm_enc, 0);
    enc.encodeSymbol(m2_enc, 7);
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    ArithmeticModel m1_dec, m2_dec;
    m1_dec.init(4, false);
    m2_dec.init(8, false);

    ArithmeticBitModel bm_dec;
    bm_dec.init();

    CHECK(dec.decodeSymbol(m1_dec) == 2);
    CHECK(dec.decodeBit(bm_dec) == 1);
    CHECK(dec.decodeSymbol(m2_dec) == 5);
    CHECK(dec.decodeSymbol(m1_dec) == 0);
    CHECK(dec.decodeBit(bm_dec) == 0);
    CHECK(dec.decodeSymbol(m2_dec) == 7);
}

TEST_CASE("ArithmeticModel adaptive updating", "[arithmetic]") {
    constexpr int N = 10000;
    constexpr std::uint32_t NUM_SYMBOLS = 8;

    std::vector<std::uint32_t> symbols(N);
    for (int i = 0; i < N; i++) {
        symbols[i] = (i < N / 2) ? 0 : 7;
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    ArithmeticModel m_enc;
    m_enc.init(NUM_SYMBOLS, true);
    for (auto s : symbols) {
        enc.encodeSymbol(m_enc, s);
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    ArithmeticModel m_dec;
    m_dec.init(NUM_SYMBOLS, false);
    for (int i = 0; i < N; i++) {
        CHECK(dec.decodeSymbol(m_dec) == symbols[i]);
    }
}

TEST_CASE("ArithmeticModel single symbol", "[arithmetic]") {
    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    ArithmeticModel m_enc;
    m_enc.init(2, true);
    for (int i = 0; i < 100; i++) {
        enc.encodeSymbol(m_enc, 0);
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    ArithmeticModel m_dec;
    m_dec.init(2, false);
    for (int i = 0; i < 100; i++) {
        CHECK(dec.decodeSymbol(m_dec) == 0);
    }
}

TEST_CASE("ArithmeticModel max symbols (2048)", "[arithmetic]") {
    constexpr std::uint32_t MAX_SYMBOLS = 2048;
    constexpr int N = 500;

    std::vector<std::uint32_t> symbols(N);
    std::srand(99999);
    for (int i = 0; i < N; i++) {
        symbols[i] = std::rand() % MAX_SYMBOLS;
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    ArithmeticModel m_enc;
    m_enc.init(MAX_SYMBOLS, true);
    for (auto s : symbols) {
        enc.encodeSymbol(m_enc, s);
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    ArithmeticModel m_dec;
    m_dec.init(MAX_SYMBOLS, false);
    for (int i = 0; i < N; i++) {
        CHECK(dec.decodeSymbol(m_dec) == symbols[i]);
    }
}

TEST_CASE("ArithmeticBitModel init resets state", "[arithmetic]") {
    ArithmeticBitModel bm;
    bm.init();
    CHECK(bm.bit_0_prob == (1U << (BM__LengthShift - 1)));
    CHECK(bm.bit_0_count == 1);
    CHECK(bm.bit_count == 2);
    CHECK(bm.update_cycle == 4);
    CHECK(bm.bits_until_update == 4);
}

TEST_CASE("Mixed raw and model-based encoding", "[arithmetic]") {
    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    ArithmeticModel m_enc;
    m_enc.init(4, true);

    enc.writeInt(0xCAFEBABE);
    enc.encodeSymbol(m_enc, 3);
    enc.writeByte(0x55);
    enc.encodeSymbol(m_enc, 1);
    enc.writeShort(0x1234);
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    ArithmeticModel m_dec;
    m_dec.init(4, false);

    CHECK(dec.readInt() == 0xCAFEBABE);
    CHECK(dec.decodeSymbol(m_dec) == 3);
    CHECK(dec.readByte() == 0x55);
    CHECK(dec.decodeSymbol(m_dec) == 1);
    CHECK(dec.readShort() == 0x1234);
}
