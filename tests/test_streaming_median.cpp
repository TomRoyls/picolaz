#include <catch2/catch_test_macros.hpp>
#include <picolaz/compressor_types.hpp>

using namespace picolaz;

TEST_CASE("StreamingMedian5 initial state", "[streaming_median]") {
    StreamingMedian5 m;
    m.init();
    CHECK(m.get() == 0);
    for (int i = 0; i < 5; i++) {
        CHECK(m.values[i] == 0);
    }
}

TEST_CASE("StreamingMedian5 after adding one value", "[streaming_median]") {
    StreamingMedian5 m;
    m.init();
    m.add(10);
    CHECK(m.get() == 0);
}

TEST_CASE("StreamingMedian5 after adding five values", "[streaming_median]") {
    StreamingMedian5 m;
    m.init();

    SECTION("ascending values") {
        m.add(1);
        m.add(2);
        m.add(3);
        m.add(4);
        m.add(5);
        std::int32_t med = m.get();
        CHECK(med >= 0);
        CHECK(med <= 5);
    }

    SECTION("descending values") {
        m.add(5);
        m.add(4);
        m.add(3);
        m.add(2);
        m.add(1);
        std::int32_t med = m.get();
        CHECK(med >= 0);
        CHECK(med <= 5);
    }
}

TEST_CASE("StreamingMedian5 all same values", "[streaming_median]") {
    StreamingMedian5 m;
    m.init();
    for (int i = 0; i < 10; i++) {
        m.add(42);
    }
    CHECK(m.get() == 42);
}

TEST_CASE("StreamingMedian5 alternating high/low", "[streaming_median]") {
    StreamingMedian5 m;
    m.init();
    m.add(100);
    m.add(-100);
    m.add(100);
    m.add(-100);
    m.add(100);
    std::int32_t med = m.get();
    CHECK(med >= -100);
    CHECK(med <= 100);
}

TEST_CASE("StreamingMedian5 negative values", "[streaming_median]") {
    StreamingMedian5 m;
    m.init();
    m.add(-5);
    m.add(-3);
    m.add(-1);
    m.add(-7);
    m.add(-9);
    std::int32_t med = m.get();
    CHECK(med >= -9);
    CHECK(med <= -1);
}

TEST_CASE("StreamingMedian5 many values converge", "[streaming_median]") {
    StreamingMedian5 m;
    m.init();
    for (int i = 0; i < 100; i++) {
        m.add(50 + (i % 5) - 2);
    }
    std::int32_t med = m.get();
    CHECK(med >= 48);
    CHECK(med <= 52);
}

TEST_CASE("StreamingMedian5 high flag toggles", "[streaming_median]") {
    StreamingMedian5 m;
    m.init();
    CHECK(m.high == true);

    m.add(10);
    CHECK(m.high == false);

    m.add(-10);
    CHECK(m.high == true);
}

TEST_CASE("StreamingMedian5 large values", "[streaming_median]") {
    StreamingMedian5 m;
    m.init();
    m.add(1000000);
    m.add(2000000);
    m.add(3000000);
    m.add(4000000);
    m.add(5000000);
    std::int32_t med = m.get();
    CHECK(med >= 0);
    CHECK(med <= 5000000);
}
