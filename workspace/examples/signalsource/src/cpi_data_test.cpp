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
            std::cerr << "usage: cpi-data-test <CPI-directory>\n";
            return 2;
        }
        const std::filesystem::path directory{argv[1]};
        const auto cpi = radar_example::load_cpi(directory);
        std::ifstream input(directory / "input.bin", std::ios::binary);
        const std::vector<char> original{
            std::istreambuf_iterator<char>(input), {}};
        if (cpi.metadata.cpi_index != 0 ||
            cpi.metadata.channel_count != 1 ||
            cpi.metadata.samples_per_channel != 751206 ||
            cpi.metadata.pulse_count != 64 ||
            cpi.metadata.wave_process_type != 4 ||
            cpi.metadata.velocity_oversampling != 2 ||
            cpi.cs16.size() != 3004824 ||
            original.size() != cpi.cs16.size() ||
            std::memcmp(original.data(), cpi.cs16.data(), original.size()) != 0 ||
            2136U + cpi.cs16.size() != 3006960U) {
            std::cerr << "CPI dimensions or CS16 bytes changed\n";
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
        for (std::size_t index = 0; index < 64; ++index) {
            if (cpi.metadata.pulse_time_offset_s[index] != time[index] ||
                cpi.metadata.pulse_phase_rad[index] != phase[index] ||
                cpi.metadata.pulse_frequency_hz[index] != frequency[index] ||
                cpi.metadata.coherent_weight[index] != weight[index]) {
                std::cerr << "CPI pulse metadata changed at index "
                          << index << '\n';
                return 1;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cpi-data-test: " << error.what() << '\n';
        return 1;
    }
}
