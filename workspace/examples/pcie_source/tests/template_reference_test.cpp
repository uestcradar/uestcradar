#include "cpi_templates.hpp"
#include "cpi_data.hpp" // Existing signalsource implementation: independent reference.
#include <cassert>

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto actual = pcie_source::load_templates(argv[1]);
    const auto reference = radar_example::load_cpi_sequence(argv[1]);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const auto& a = actual[i];
        const auto& b = reference[i].metadata;
        assert(a.cpi_index == b.cpi_index);
        assert(radar_example::same_waveform_configuration(a, b));
        assert(a.pulse_time_offset_s == b.pulse_time_offset_s);
        assert(a.pulse_phase_rad == b.pulse_phase_rad);
        assert(a.pulse_frequency_hz == b.pulse_frequency_hz);
        assert(a.coherent_weight == b.coherent_weight);
    }
}
