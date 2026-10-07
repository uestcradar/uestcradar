#include "cpi_data.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: cpi-data-test <CPI-root>\n";
            return 2;
        }
        const std::filesystem::path root{argv[1]};
        const auto cpis = radar_example::load_cpi_sequence(root);
        if (cpis.size() != radar_example::kOfflineCpiCount) {
            std::cerr << "offline CPI count changed\n";
            return 1;
        }
        for (std::size_t cpi_index = 0;
             cpi_index < cpis.size();
             ++cpi_index) {
            const auto& cpi = cpis[cpi_index];
            const auto directory = root / ("CPI" + std::to_string(cpi_index));
            std::ifstream input(directory / "input.bin", std::ios::binary);
            const std::vector<char> original{
                std::istreambuf_iterator<char>(input), {}};
            if (cpi.metadata.cpi_index != cpi_index ||
                cpi.metadata.channel_count != 1 ||
                cpi.metadata.samples_per_channel !=
                    radar_example::kOfflineCpiSamples ||
                cpi.metadata.pulse_count !=
                    radar_example::kOfflineCpiPulses ||
                cpi.metadata.wave_process_type != 4 ||
                cpi.metadata.velocity_oversampling != 2 ||
                cpi.cs16.size() != radar_example::kOfflineCpiCs16Bytes ||
                original.size() != cpi.cs16.size() ||
                std::memcmp(
                    original.data(), cpi.cs16.data(), original.size()) != 0 ||
                radar_example::kIQV3MetadataBytes + cpi.cs16.size() !=
                    radar_example::kIQV3PayloadBytes) {
                std::cerr << "CPI dimensions or CS16 bytes changed for CPI"
                          << cpi_index << '\n';
                return 1;
            }
            const auto time = radar_example::read_double_lines(
                directory / "pulse_time.txt");
            const auto phase = radar_example::read_double_lines(
                directory / "pulse_phase.txt");
            const auto frequency = radar_example::read_double_lines(
                directory / "pulse_freq.txt");
            const auto weight = radar_example::read_double_lines(
                directory / "wd0.txt");
            for (std::size_t pulse = 0;
                 pulse < radar_example::kOfflineCpiPulses;
                 ++pulse) {
                if (cpi.metadata.pulse_time_offset_s[pulse] != time[pulse] ||
                    cpi.metadata.pulse_phase_rad[pulse] != phase[pulse] ||
                    cpi.metadata.pulse_frequency_hz[pulse] != frequency[pulse] ||
                    cpi.metadata.coherent_weight[pulse] != weight[pulse]) {
                    std::cerr << "CPI pulse metadata changed at CPI"
                              << cpi_index << " pulse " << pulse << '\n';
                    return 1;
                }
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cpi-data-test: " << error.what() << '\n';
        return 1;
    }
}
