#include <catch2/catch_test_macros.hpp>
#include <picolaz/streams.hpp>
#include <utility>

using namespace picolaz;

TEST_CASE("ByteStreamOut basic write and read back", "[streams]") {
    ByteStreamOut out;

    SECTION("write single bytes") {
        out.put_byte(0x42);
        out.put_byte(0xFF);
        out.put_byte(0x00);

        CHECK(out.size() == 3);
        CHECK(out.data()[0] == 0x42);
        CHECK(out.data()[1] == 0xFF);
        CHECK(out.data()[2] == 0x00);
    }

    SECTION("write byte array") {
        std::uint8_t data[] = {1, 2, 3, 4, 5};
        out.put_bytes(data, 5);

        CHECK(out.size() == 5);
        for (std::uint32_t i = 0; i < 5; i++) {
            CHECK(out.data()[i] == i + 1);
        }
    }

    SECTION("position tracking") {
        CHECK(out.position() == 0);
        out.put_byte(0x01);
        CHECK(out.position() == 1);
        out.put_byte(0x02);
        CHECK(out.position() == 2);
        std::uint8_t data[10] = {};
        out.put_bytes(data, 10);
        CHECK(out.position() == 12);
    }

    SECTION("clear resets stream") {
        out.put_byte(0x01);
        out.put_byte(0x02);
        CHECK(out.size() == 2);
        out.clear();
        CHECK(out.size() == 0);
        CHECK(out.position() == 0);
    }

    SECTION("seek truncates buffer") {
        out.put_byte(0x01);
        out.put_byte(0x02);
        out.put_byte(0x03);
        out.seek(1);
        CHECK(out.size() == 1);
        CHECK(out.data()[0] == 0x01);
    }
}

TEST_CASE("ByteStreamOut 16/32/64 bit writes", "[streams]") {
    ByteStreamOut out;

    SECTION("put_16bits_le") {
        std::uint16_t val = 0x1234;
        out.put_16bits_le(reinterpret_cast<const std::uint8_t*>(&val));
        CHECK(out.size() == 2);
        CHECK(out.data()[0] == 0x34);
        CHECK(out.data()[1] == 0x12);
    }

    SECTION("put_32bits_le") {
        std::uint32_t val = 0x12345678;
        out.put_32bits_le(reinterpret_cast<const std::uint8_t*>(&val));
        CHECK(out.size() == 4);
        CHECK(out.data()[0] == 0x78);
        CHECK(out.data()[1] == 0x56);
        CHECK(out.data()[2] == 0x34);
        CHECK(out.data()[3] == 0x12);
    }

    SECTION("put_64bits_le") {
        std::uint64_t val = 0x0102030405060708ULL;
        out.put_64bits_le(reinterpret_cast<const std::uint8_t*>(&val));
        CHECK(out.size() == 8);
        CHECK(out.data()[0] == 0x08);
        CHECK(out.data()[7] == 0x01);
    }
}

TEST_CASE("ByteStreamIn basic read", "[streams]") {
    std::uint8_t data[] = {0x42, 0xFF, 0x00, 0xAB};
    ByteStreamIn in(data, 4);

    SECTION("read single bytes") {
        CHECK(in.get_byte() == 0x42);
        CHECK(in.get_byte() == 0xFF);
        CHECK(in.get_byte() == 0x00);
        CHECK(in.get_byte() == 0xAB);
    }

    SECTION("position tracking") {
        CHECK(in.position() == 0);
        CHECK(in.remaining() == 4);
        in.get_byte();
        CHECK(in.position() == 1);
        CHECK(in.remaining() == 3);
        in.get_byte();
        in.get_byte();
        in.get_byte();
        CHECK(in.position() == 4);
        CHECK(in.remaining() == 0);
    }

    SECTION("read past end returns 0") {
        for (int i = 0; i < 4; i++) in.get_byte();
        CHECK(in.get_byte() == 0);
    }

    SECTION("get_bytes") {
        std::uint8_t buf[4];
        in.get_bytes(buf, 4);
        CHECK(buf[0] == 0x42);
        CHECK(buf[1] == 0xFF);
        CHECK(buf[2] == 0x00);
        CHECK(buf[3] == 0xAB);
    }

    SECTION("skip_bytes") {
        in.skip_bytes(2);
        CHECK(in.position() == 2);
        CHECK(in.get_byte() == 0x00);
    }
}

TEST_CASE("ByteStreamIn 16/32/64 bit reads", "[streams]") {
    SECTION("get_16bits_le") {
        std::uint8_t data[] = {0x34, 0x12};
        ByteStreamIn in(data, 2);
        std::uint16_t val = 0;
        in.get_16bits_le(reinterpret_cast<std::uint8_t*>(&val));
        CHECK(val == 0x1234);
    }

    SECTION("get_32bits_le") {
        std::uint8_t data[] = {0x78, 0x56, 0x34, 0x12};
        ByteStreamIn in(data, 4);
        std::uint32_t val = 0;
        in.get_32bits_le(reinterpret_cast<std::uint8_t*>(&val));
        CHECK(val == 0x12345678);
    }

    SECTION("get_64bits_le") {
        std::uint8_t data[] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
        ByteStreamIn in(data, 8);
        std::uint64_t val = 0;
        in.get_64bits_le(reinterpret_cast<std::uint8_t*>(&val));
        CHECK(val == 0x0102030405060708ULL);
    }
}

TEST_CASE("ByteStreamOut/In roundtrip", "[streams]") {
    ByteStreamOut out;

    std::uint16_t v16 = 0xBEEF;
    std::uint32_t v32 = 0xDEADBEEF;
    std::uint64_t v64 = 0x0123456789ABCDEFULL;

    out.put_byte(0x42);
    out.put_16bits_le(reinterpret_cast<const std::uint8_t*>(&v16));
    out.put_32bits_le(reinterpret_cast<const std::uint8_t*>(&v32));
    out.put_64bits_le(reinterpret_cast<const std::uint8_t*>(&v64));

    ByteStreamIn in(out.data(), out.size());

    CHECK(in.get_byte() == 0x42);

    std::uint16_t r16 = 0;
    in.get_16bits_le(reinterpret_cast<std::uint8_t*>(&r16));
    CHECK(r16 == v16);

    std::uint32_t r32 = 0;
    in.get_32bits_le(reinterpret_cast<std::uint8_t*>(&r32));
    CHECK(r32 == v32);

    std::uint64_t r64 = 0;
    in.get_64bits_le(reinterpret_cast<std::uint8_t*>(&r64));
    CHECK(r64 == v64);

    CHECK(in.remaining() == 0);
}

TEST_CASE("ByteStreamIn init reinitializes stream", "[streams]") {
    std::uint8_t data1[] = {1, 2, 3};
    std::uint8_t data2[] = {4, 5};

    ByteStreamIn in(data1, 3);
    CHECK(in.get_byte() == 1);

    in.init(data2, 2);
    CHECK(in.position() == 0);
    CHECK(in.remaining() == 2);
    CHECK(in.get_byte() == 4);
    CHECK(in.get_byte() == 5);
}

TEST_CASE("ByteStreamIn get_bytes past end zero-fills", "[streams]") {
    std::uint8_t data[] = {0xAA, 0xBB};
    ByteStreamIn in(data, 2);

    std::uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    in.get_bytes(buf, 4);

    CHECK(buf[0] == 0xAA);
    CHECK(buf[1] == 0xBB);
    CHECK(buf[2] == 0x00);
    CHECK(buf[3] == 0x00);
}

TEST_CASE("ByteStreamOut buffer access", "[streams]") {
    ByteStreamOut out;
    out.put_byte(1);
    out.put_byte(2);

    auto& buf = out.buffer();
    CHECK(buf.size() == 2);
    CHECK(buf[0] == 1);
    CHECK(buf[1] == 2);

    const auto& cbuf = std::as_const(out).buffer();
    CHECK(cbuf.size() == 2);
}
