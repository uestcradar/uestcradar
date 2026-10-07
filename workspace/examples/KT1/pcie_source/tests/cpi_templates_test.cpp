#include "cpi_templates.hpp"
#include <cassert>
#include <unistd.h>

int main(int argc, char** argv) {
    using namespace pcie_source;
    if (argc == 2) { (void)load_templates(argv[1]); return 0; }
    auto path = std::filesystem::temp_directory_path() / "pcie-templates-XXXXXX";
    auto name = path.string();
    assert(mkdtemp(name.data()) != nullptr);
    path = name;
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{path};
    const auto write_metadata = [&](unsigned cpi, unsigned points = samples_per_cpi) {
        std::ofstream file(path / ("CPI" + std::to_string(cpi)) / "metadata.json");
        file << R"({"format_version":2,"input_sample_bytes":4,"input_layout":"CS16 little-endian: int16 I,int16 Q,I,Q,...",)"
             << "\"sample_count\":" << points << ",\"cpi_index\":" << cpi
             << R"(,"pulse_n":64,"wave_process_type":4,"velocity_oversampling":1,"sample_rate_hz":30720000,"carrier_frequency_hz":3000000000,"bandwidth_hz":2000000,"pulse_width_s":0.00001,"prt_s":0.0002,"observation_max_range_m":10000,"input_dequantization_scale":0.5})";
    };
    for (unsigned cpi = 0; cpi < 10; ++cpi) {
        const auto directory = path / ("CPI" + std::to_string(cpi));
        std::filesystem::create_directory(directory);
        write_metadata(cpi);
        for (auto file : {"pulse_time.txt", "pulse_phase.txt", "pulse_freq.txt", "wd0.txt"}) {
            std::ofstream output(directory / file);
            for (unsigned pulse = 0; pulse < 64; ++pulse) {
                if (std::string(file) == "pulse_freq.txt") output << 3000000000ULL + cpi;
                else if (std::string(file) == "pulse_time.txt") output << pulse * 0.0002;
                else output << cpi;
                output << '\n';
            }
        }
    }
    auto templates = load_templates(path);
    assert(templates[9].samples_per_channel == samples_per_cpi);
    assert(templates[9].pulse_phase_rad[63] == 9);
    assert(templates[9].pulse_frequency_hz[0] == 3000000009.0);
    assert(!std::filesystem::exists(path / "CPI0/input.bin"));
    const auto fails = [&] {
        try { (void)load_templates(path); } catch (const std::runtime_error&) { return true; }
        return false;
    };
    write_metadata(0, 4096); assert(fails()); write_metadata(0);
    { std::ofstream file(path / "CPI0/metadata.json", std::ios::app); file << "garbage"; }
    assert(fails()); write_metadata(0);
    { std::ofstream file(path / "CPI0/wd0.txt", std::ios::app); file << "1\n"; }
    assert(fails());
    std::filesystem::remove(path / "CPI1/metadata.json"); assert(fails());
}
