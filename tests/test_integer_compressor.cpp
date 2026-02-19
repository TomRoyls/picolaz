#include <catch2/catch_test_macros.hpp>
#include <picolaz/integer_compressor.hpp>
#include <picolaz/streams.hpp>
#include <vector>
#include <cstdlib>

using namespace picolaz;

TEST_CASE("IntegerCompressor constant sequence", "[integer_compressor]") {
    constexpr int N = 200;
    constexpr i32 VALUE = 42;

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    IntegerCompressor ic_enc(&enc, 16, 1);
    ic_enc.initCompressor();

    for (int i = 0; i < N; i++) {
        ic_enc.compress(VALUE, VALUE);
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    IntegerCompressor ic_dec(&dec, 16, 1);
    ic_dec.initDecompressor();

    for (int i = 0; i < N; i++) {
        i32 val = ic_dec.decompress(VALUE);
        CHECK(val == VALUE);
    }
}

TEST_CASE("IntegerCompressor incrementing sequence", "[integer_compressor]") {
    constexpr int N = 500;

    std::vector<i32> values(N);
    for (int i = 0; i < N; i++) {
        values[i] = i * 10;
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    IntegerCompressor ic_enc(&enc, 16, 1);
    ic_enc.initCompressor();

    i32 pred = 0;
    for (int i = 0; i < N; i++) {
        ic_enc.compress(pred, values[i]);
        pred = values[i];
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    IntegerCompressor ic_dec(&dec, 16, 1);
    ic_dec.initDecompressor();

    pred = 0;
    for (int i = 0; i < N; i++) {
        i32 val = ic_dec.decompress(pred);
        CHECK(val == values[i]);
        pred = val;
    }
}

TEST_CASE("IntegerCompressor random 16-bit values", "[integer_compressor]") {
    constexpr int N = 1000;

    std::vector<i32> values(N);
    std::srand(42);
    for (int i = 0; i < N; i++) {
        values[i] = std::rand() % 65536;
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    IntegerCompressor ic_enc(&enc, 16, 1);
    ic_enc.initCompressor();

    i32 pred = 0;
    for (int i = 0; i < N; i++) {
        ic_enc.compress(pred, values[i]);
        pred = values[i];
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    IntegerCompressor ic_dec(&dec, 16, 1);
    ic_dec.initDecompressor();

    pred = 0;
    for (int i = 0; i < N; i++) {
        i32 val = ic_dec.decompress(pred);
        CHECK(val == values[i]);
        pred = val;
    }
}

TEST_CASE("IntegerCompressor multiple contexts", "[integer_compressor]") {
    constexpr int N = 200;
    constexpr u32 NUM_CONTEXTS = 4;

    std::vector<std::vector<i32>> values(NUM_CONTEXTS);
    std::srand(777);
    for (u32 c = 0; c < NUM_CONTEXTS; c++) {
        values[c].resize(N);
        for (int i = 0; i < N; i++) {
            values[c][i] = std::rand() % 1000;
        }
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    IntegerCompressor ic_enc(&enc, 16, NUM_CONTEXTS);
    ic_enc.initCompressor();

    std::vector<i32> pred(NUM_CONTEXTS, 0);
    for (int i = 0; i < N; i++) {
        for (u32 c = 0; c < NUM_CONTEXTS; c++) {
            ic_enc.compress(pred[c], values[c][i], c);
            pred[c] = values[c][i];
        }
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    IntegerCompressor ic_dec(&dec, 16, NUM_CONTEXTS);
    ic_dec.initDecompressor();

    std::fill(pred.begin(), pred.end(), 0);
    for (int i = 0; i < N; i++) {
        for (u32 c = 0; c < NUM_CONTEXTS; c++) {
            i32 val = ic_dec.decompress(pred[c], c);
            CHECK(val == values[c][i]);
            pred[c] = val;
        }
    }
}

TEST_CASE("IntegerCompressor negative correctors", "[integer_compressor]") {
    constexpr int N = 300;

    std::vector<i32> values(N);
    for (int i = 0; i < N; i++) {
        values[i] = 50000 - i * 100;
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    IntegerCompressor ic_enc(&enc, 16, 1);
    ic_enc.initCompressor();

    i32 pred = 50000;
    for (int i = 0; i < N; i++) {
        ic_enc.compress(pred, values[i]);
        pred = values[i];
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    IntegerCompressor ic_dec(&dec, 16, 1);
    ic_dec.initDecompressor();

    pred = 50000;
    for (int i = 0; i < N; i++) {
        i32 val = ic_dec.decompress(pred);
        CHECK(val == values[i]);
        pred = val;
    }
}

TEST_CASE("IntegerCompressor 32-bit values", "[integer_compressor]") {
    constexpr int N = 100;

    std::vector<i32> values(N);
    std::srand(1234);
    for (int i = 0; i < N; i++) {
        values[i] = static_cast<i32>(std::rand());
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    IntegerCompressor ic_enc(&enc, 32, 1);
    ic_enc.initCompressor();

    i32 pred = 0;
    for (int i = 0; i < N; i++) {
        ic_enc.compress(pred, values[i]);
        pred = values[i];
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    IntegerCompressor ic_dec(&dec, 32, 1);
    ic_dec.initDecompressor();

    pred = 0;
    for (int i = 0; i < N; i++) {
        i32 val = ic_dec.decompress(pred);
        CHECK(val == values[i]);
        pred = val;
    }
}

TEST_CASE("IntegerCompressor zero bits", "[integer_compressor]") {
    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    IntegerCompressor ic_enc(&enc, 0, 1);
    ic_enc.initCompressor();

    ic_enc.compress(0, 0);
    ic_enc.compress(0, 0);
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    IntegerCompressor ic_dec(&dec, 0, 1);
    ic_dec.initDecompressor();

    CHECK(ic_dec.decompress(0) == 0);
    CHECK(ic_dec.decompress(0) == 0);
}

TEST_CASE("IntegerCompressor small range values", "[integer_compressor]") {
    constexpr int N = 500;

    std::vector<i32> values(N);
    std::srand(5555);
    for (int i = 0; i < N; i++) {
        values[i] = std::rand() % 10;
    }

    ByteStreamOut out;
    ArithmeticEncoder enc;
    enc.init(&out);

    IntegerCompressor ic_enc(&enc, 8, 1);
    ic_enc.initCompressor();

    i32 pred = 0;
    for (int i = 0; i < N; i++) {
        ic_enc.compress(pred, values[i]);
        pred = values[i];
    }
    enc.done();

    ByteStreamIn in(out.data(), out.size());
    ArithmeticDecoder dec;
    dec.init(&in);

    IntegerCompressor ic_dec(&dec, 8, 1);
    ic_dec.initDecompressor();

    pred = 0;
    for (int i = 0; i < N; i++) {
        i32 val = ic_dec.decompress(pred);
        CHECK(val == values[i]);
        pred = val;
    }
}
