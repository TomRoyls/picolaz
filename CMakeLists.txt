# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

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

# Compiler warnings
if(MSVC)
    target_compile_options(picolaz PRIVATE /W4)
else()
    target_compile_options(picolaz PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Only build tests if this is the top-level project (not included via FetchContent)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(PICOLAZ_BUILD_TESTS "Build picolaz tests" ON)
else()
    option(PICOLAZ_BUILD_TESTS "Build picolaz tests" OFF)
endif()

# Examples
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(PICOLAZ_BUILD_EXAMPLES "Build picolaz examples" ON)
else()
    option(PICOLAZ_BUILD_EXAMPLES "Build picolaz examples" OFF)
endif()

if(PICOLAZ_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Install library
install(TARGETS picolaz
    EXPORT picolazTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY include/picolaz
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Install exported targets
install(EXPORT picolazTargets
    FILE picolazTargets.cmake
    NAMESPACE picolaz::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/picolaz
)

# Generate package config file
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/picolazConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/picolazConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/picolaz
)

# Generate package version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/picolazConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/picolazConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/picolazConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/picolaz
)
