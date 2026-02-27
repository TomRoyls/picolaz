cmake_minimum_required(VERSION 3.20)

project(picolaz_examples CXX)

# Examples
add_executable(picolaz_write_example write_laz.cpp)
add_executable(picolaz_read_example read_laz.cpp)

target_link_libraries(picolaz_write_example PRIVATE picolaz)
target_link_libraries(picolaz_read_example PRIVATE picolaz)

target_compile_features(picolaz_write_example PRIVATE cxx_std_20)
target_compile_features(picolaz_read_example PRIVATE cxx_std_20)
