cmake_minimum_required(VERSION 3.20)
project(picolaz VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(picolaz STATIC
    src/arithmetic_codec.cpp
    src/integer_compressor.cpp
    src/point14_compressor.cpp
    src/point14_decompressor.cpp
    src/rgb14_compressor.cpp
    src/rgb14_decompressor.cpp
    src/rgbnir14_compressor.cpp
    src/rgbnir14_decompressor.cpp
    src/laz_writer.cpp
    src/laz_reader.cpp
)

target_include_directories(picolaz
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(picolaz PUBLIC cxx_std_20)

# Only build tests if this is the top-level project (not included via FetchContent)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(PICOLAZ_BUILD_TESTS "Build picolaz tests" ON)
else()
    option(PICOLAZ_BUILD_TESTS "Build picolaz tests" OFF)
endif()

if(PICOLAZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
