// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <picolaz/picolaz.hpp>
#include <cmath>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <output.laz>" << std::endl;
        return 1;
    }

    try {
        // Configure writer
        picolaz::LazWriter::Config config;
        config.format = picolaz::PointFormat::Format8;  // RGB + NIR
        config.chunk_size = 50000;
        config.x_scale = 0.001;
        config.y_scale = 0.001;
        config.z_scale = 0.001;

        // Create writer
        picolaz::LazWriter writer(argv[1], config);

        // Create sample points
        std::vector<picolaz::Point14> points;
        for (int i = 0; i < 10000; ++i) {
            picolaz::Point14 pt;
            pt.x = i * 100;
            pt.y = i * 200;
            pt.z = static_cast<std::int32_t>(1000 + 100 * sin(i * 0.01));
            pt.intensity = static_cast<std::uint16_t>(1000 + 500 * cos(i * 0.02));
            pt.return_number = 1;
            pt.number_of_returns = 1;
            pt.classification = (i % 10 == 0) ? 2 : 1;  // Ground or unclassified
            pt.gps_time = 1000000.0 + i * 0.001;
            pt.red = static_cast<std::uint16_t>(30000 + 20000 * sin(i * 0.005));
            pt.green = static_cast<std::uint16_t>(40000 + 15000 * cos(i * 0.007));
            pt.blue = static_cast<std::uint16_t>(50000);
            pt.nir = static_cast<std::uint16_t>(25000 + 10000 * sin(i * 0.003));
            points.push_back(pt);
        }

        // Write points
        writer.write_points(points);
        writer.close();

        std::cout << "Written " << points.size() << " points to " << argv[1] << std::endl;
        return 0;

    } catch (const picolaz::LazError& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
