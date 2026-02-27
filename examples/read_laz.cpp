// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <picolaz/picolaz.hpp>
#include <iostream>
#include <iomanip>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input.laz>" << std::endl;
        return 1;
    }

    try {
        // Open file
        picolaz::LazReader reader(argv[1]);

        // Print header info
        const auto& header = reader.header();
        std::cout << "=== LAZ File Information ===" << std::endl;
        std::cout << "LAS Version: " << static_cast<int>(header.version_major) 
                  << "." << static_cast<int>(header.version_minor) << std::endl;
        std::cout << "Point Format: " << static_cast<int>(reader.format()) << std::endl;
        std::cout << "Point Count: " << reader.point_count() << std::endl;
        std::cout << "Point Record Length: " << header.point_data_record_length << " bytes" << std::endl;
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "Scale: (" << header.x_scale_factor << ", " 
                  << header.y_scale_factor << ", " << header.z_scale_factor << ")" << std::endl;
        std::cout << "Offset: (" << header.x_offset << ", " 
                  << header.y_offset << ", " << header.z_offset << ")" << std::endl;
        std::cout << "Bounds X: [" << header.min_x << ", " << header.max_x << "]" << std::endl;
        std::cout << "Bounds Y: [" << header.min_y << ", " << header.max_y << "]" << std::endl;
        std::cout << "Bounds Z: [" << header.min_z << ", " << header.max_z << "]" << std::endl;
        std::cout << std::endl;

        // Read and print first 10 points
        std::cout << "=== First 10 Points ===" << std::endl;
        picolaz::Point14 pt;
        int count = 0;
        while (reader.read_point(pt) && count < 10) {
            std::cout << "Point " << count << ": "
                      << "X=" << pt.x << " Y=" << pt.y << " Z=" << pt.z
                      << " I=" << pt.intensity
                      << " GPS=" << std::setprecision(3) << pt.gps_time;
            
            if (picolaz::format_has_rgb(reader.format())) {
                std::cout << " RGB=(" << pt.red << "," << pt.green << "," << pt.blue << ")";
            }
            if (picolaz::format_has_nir(reader.format())) {
                std::cout << " NIR=" << pt.nir;
            }
            std::cout << std::endl;
            ++count;
        }

        reader.close();
        return 0;

    } catch (const picolaz::LazError& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
