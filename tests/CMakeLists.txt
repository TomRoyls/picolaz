include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

set(TEST_SOURCES
    test_types.cpp
    test_streams.cpp
    test_streaming_median.cpp
    test_arithmetic_codec.cpp
    test_integer_compressor.cpp
    test_roundtrip.cpp
)

add_executable(picolaz_tests ${TEST_SOURCES})
target_link_libraries(picolaz_tests PRIVATE picolaz Catch2::Catch2WithMain)
target_compile_features(picolaz_tests PRIVATE cxx_std_20)

catch_discover_tests(picolaz_tests)
