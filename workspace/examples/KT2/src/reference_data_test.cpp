#include "cpi_reference.hpp"
#include "my_pulsecompression.hpp"

#include "../../signalsource/src/cpi_data.hpp"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("expected CPI data root");
        }
        const auto cpis = radar_example::load_cpi_sequence(
            std::filesystem::path{argv[1]});
        require(
            cpis.size() == radar_example::kReferences.size(),
            "reference CPI count mismatch");

        for (std::size_t index = 0; index < cpis.size(); ++index) {
            const auto& cpi = cpis[index];
            const auto& reference = radar_example::kReferences[index];
            require(
                radar_example::metadata_sha256(cpi.metadata) ==
                    reference.metadata_sha256,
                "CPI" + std::to_string(index) + " metadata mismatch");

            std::vector<uestcradar::ComplexInt16> input(
                cpi.cs16.size() / sizeof(uestcradar::ComplexInt16));
            std::memcpy(input.data(), cpi.cs16.data(), cpi.cs16.size());
            require(
                radar_example::sample_sha256(input) ==
                    reference.input_sha256,
                "CPI" + std::to_string(index) + " input mismatch");

            std::vector<uestcradar::ComplexFloat32> output(input.size());
            radar_example::dequantize_samples(
                {input.data(), 1, input.size()},
                {output.data(), 1, output.size()},
                cpi.metadata.dequantization_scale);
            require(
                radar_example::output_sha256(output) ==
                    reference.output_sha256,
                "CPI" + std::to_string(index) + " output mismatch");
        }
        std::cout << "reference-data-test: PASS CPI0-CPI9\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "reference-data-test: " << error.what() << '\n';
        return 1;
    }
}
