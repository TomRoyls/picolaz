# picolaz

A modern C++20 library for reading and writing LAZ 1.4 (LASzip) compressed LiDAR point cloud files.

## Features

- **LAS 1.4 Support**: Full support for point formats 6, 7, and 8
- **LAZ Compression**: Native LAZ 1.4 compression using POINTWISE_CHUNKED arithmetic coding
- **Clean API**: Simple, modern C++20 interface with RAII
- **Zero External Dependencies**: Single static library, no runtime dependencies
- **Cross-Platform**: Linux, macOS, Windows

## Supported Point Formats

| Format | Size | Fields |
|--------|------|--------|
| 6 | 30 bytes | X, Y, Z, intensity, return info, classification, scan angle, GPS time |
| 7 | 36 bytes | Format 6 + RGB |
| 8 | 38 bytes | Format 7 + NIR |

## Requirements

- C++20 compiler (GCC 10+, Clang 12+, MSVC 19.28+)
- CMake 3.20+

## Building

```bash
# Clone the repository
git clone https://github.com/yourorg/picolaz.git
cd picolaz

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run tests (optional)
ctest --test-dir build --output-on-failure

# Install (optional)
sudo cmake --install build
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `PICOLAZ_BUILD_TESTS` | ON (standalone) | Build the test suite |

## Usage

### Writing LAZ Files

```cpp
#include <picolaz/picolaz.hpp>
#include <vector>

int main() {
    // Configure writer
    picolaz::LazWriter::Config config;
    config.format = picolaz::PointFormat::Format8;  // RGB + NIR
    config.chunk_size = 50000;                       // Points per chunk
    config.x_scale = 0.001;
    config.y_scale = 0.001;
    config.z_scale = 0.001;

    // Create writer
    picolaz::LazWriter writer("output.laz", config);

    // Create points
    std::vector<picolaz::Point14> points;
    for (int i = 0; i < 1000; ++i) {
        picolaz::Point14 pt;
        pt.x = i * 100;
        pt.y = i * 200;
        pt.z = i * 10;
        pt.intensity = 1000;
        pt.return_number = 1;
        pt.number_of_returns = 1;
        pt.classification = 2;  // Ground
        pt.gps_time = 1000000.0 + i * 0.001;
        pt.red = 65535;
        pt.green = 32768;
        pt.blue = 16384;
        pt.nir = 49152;
        points.push_back(pt);
    }

    // Write points (batch or individual)
    writer.write_points(points);
    // Or: writer.write_point(single_point);

    writer.close();
    return 0;
}
```

### Reading LAZ Files

```cpp
#include <picolaz/picolaz.hpp>
#include <iostream>

int main() {
    // Open file
    picolaz::LazReader reader("input.laz");

    // Get file info
    std::cout << "Point count: " << reader.point_count() << std::endl;
    std::cout << "Format: " << static_cast<int>(reader.format()) << std::endl;

    // Access header
    const auto& header = reader.header();
    std::cout << "Scale: " << header.x_scale_factor << ", "
              << header.y_scale_factor << ", " << header.z_scale_factor << std::endl;

    // Read all points
    auto points = reader.read_points(reader.point_count());
    for (const auto& pt : points) {
        std::cout << "X=" << pt.x << " Y=" << pt.y << " Z=" << pt.z << std::endl;
    }

    // Or read point by point
    picolaz::Point14 pt;
    while (reader.read_point(pt)) {
        // Process point...
    }

    reader.close();
    return 0;
}
```

### Integration with CMake

#### Using FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    picolaz
    GIT_REPOSITORY https://github.com/yourorg/picolaz.git
    GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(picolaz)

target_link_libraries(your_target PRIVATE picolaz)
```

#### After Installation

```cmake
find_package(picolaz REQUIRED)
target_link_libraries(your_target PRIVATE picolaz::picolaz)
```

## API Reference

### Point14 Structure

```cpp
struct Point14 {
    std::int32_t x, y, z;           // Scaled coordinates
    std::uint16_t intensity;        // Intensity value
    std::uint8_t return_number;     // Return number (0-15)
    std::uint8_t number_of_returns; // Number of returns (0-15)
    std::uint8_t classification_flags;
    std::uint8_t scanner_channel;   // Scanner channel (0-3)
    std::uint8_t scan_direction_flag;
    std::uint8_t edge_of_flight_line;
    std::uint8_t classification;    // ASPRS classification
    std::uint8_t user_data;
    std::int16_t scan_angle;        // Scan angle (0.006° units)
    std::uint16_t point_source_id;
    double gps_time;                // GPS timestamp
    std::uint16_t red, green, blue; // RGB (formats 7, 8)
    std::uint16_t nir;              // NIR (format 8)
};
```

### Error Handling

```cpp
try {
    picolaz::LazReader reader("file.laz");
    // ...
} catch (const picolaz::FileOpenError& e) {
    std::cerr << "Cannot open file: " << e.what() << std::endl;
} catch (const picolaz::InvalidFormatError& e) {
    std::cerr << "Invalid format: " << e.what() << std::endl;
} catch (const picolaz::LazError& e) {
    std::cerr << "LAZ error: " << e.what() << std::endl;
}
```

## Performance

- Typical compression ratio: 5-15x (depends on point density and noise)
- Compression speed: ~500k-1M points/second (single-threaded)
- Memory usage: ~10MB base + chunk buffer (configurable via chunk_size)

## Limitations

- LAS 1.4 only (no support for LAS 1.0-1.3)
- Little-endian platforms only (x86, ARM)
- Maximum chunk size: 4GB
- No spatial indexing support
- Single-threaded compression

## License

Mozilla Public License 2.0. See [LICENSE](LICENSE) for details.

## References

- [ASPRS LAS 1.4 Specification](https://www.asprs.org/divisions-committees/lidar-division/laser-las-file-format-exchange-activities)
- [LASzip](https://laszip.org/) - Original LAZ compression reference
