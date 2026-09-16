#pragma once
#include "cpi_buffer.h"
namespace radar_qt_example {
inline void save_rd_map_pgm(
    uestcradar::Array2D<float> rd_map,
    const std::filesystem::path& output_path) {
    if (rd_map.rows() == 0 || rd_map.columns() == 0) {
        throw std::invalid_argument("RD output is empty");
    }

    const auto values = rd_map.values();
    const auto [minimum, maximum] = std::minmax_element(
        values.begin(), values.end());
    if (!std::isfinite(*minimum) || !std::isfinite(*maximum)) {
        throw std::invalid_argument("RD output contains a non-finite value");
    }

    std::vector<unsigned char> pixels(values.size(), 0);
    if (*maximum > *minimum) {
        const double scale = 255.0 / (double(*maximum) - double(*minimum));
        std::transform(
            values.begin(), values.end(), pixels.begin(),
            [&](float value) {
                return static_cast<unsigned char>(std::clamp(
                    std::lround((double(value) - double(*minimum)) * scale), 0L, 255L));
            });
    }

    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    auto temporary_path = output_path;
    temporary_path += ".tmp";
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create RDMap PGM output");
    }
    output << "P5\n" << rd_map.columns() << ' ' << rd_map.rows()
           << "\n255\n";
    output.write(
        reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size()));
    output.close();
    if (!output) {
        throw std::runtime_error("cannot write RDMap PGM output");
    }
    std::filesystem::rename(temporary_path, output_path);
}

}  // namespace radar_qt_example
