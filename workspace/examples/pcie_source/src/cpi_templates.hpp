#pragma once

#include <data.h>
#include "cJSON.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace pcie_source {
inline constexpr std::size_t samples_per_cpi = 751206;
inline constexpr std::size_t template_count = 10;
using Templates = std::array<uestcradar::IQMetadata, template_count>;

// Parameter mapping follows signalsource/src/cpi_data.hpp::load_cpi.
// Use the existing migrated cJSON parser; never load offline input.bin.
inline Templates load_templates(const std::filesystem::path& root) {
    Templates templates{};
    for (std::size_t index = 0; index < template_count; ++index) {
        const auto directory = root / ("CPI" + std::to_string(index));
        std::ifstream file(directory / "metadata.json");
        if (!file) throw std::runtime_error("cannot read " + (directory / "metadata.json").string());
        const std::string text{std::istreambuf_iterator<char>(file), {}};
        const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(
            cJSON_ParseWithOpts(text.c_str(), nullptr, 1), cJSON_Delete);
        if (!json || !cJSON_IsObject(json.get())) throw std::runtime_error("invalid metadata JSON");
        // Reject duplicate fields rather than silently choosing one.
        for (auto* a = json->child; a != nullptr; a = a->next)
            for (auto* b = a->next; b != nullptr; b = b->next)
                if (std::string(a->string) == b->string)
                    throw std::runtime_error("duplicate metadata field");
        const auto number = [&](const char* name) {
            const auto* value = cJSON_GetObjectItemCaseSensitive(json.get(), name);
            if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble))
                throw std::runtime_error(std::string("invalid metadata field: ") + name);
            return value->valuedouble;
        };
        const auto integer = [&](const char* name) {
            const double n = number(name);
            if (n < 0 || n > UINT32_MAX || n != std::floor(n))
                throw std::runtime_error(std::string("invalid integer: ") + name);
            return static_cast<std::uint32_t>(n);
        };
        const auto* layout = cJSON_GetObjectItemCaseSensitive(json.get(), "input_layout");
        if (integer("format_version") != 2 || integer("input_sample_bytes") != 4 ||
            !cJSON_IsString(layout) || std::string(layout->valuestring) !=
                "CS16 little-endian: int16 I,int16 Q,I,Q,..." ||
            integer("sample_count") != samples_per_cpi || integer("pulse_n") != 64 ||
            integer("cpi_index") != index)
            throw std::runtime_error("template does not match signalsource IQ v3 shape/index");
        auto& m = templates[index];
        m.cpi_index = index;
        m.channel_count = 1;
        m.samples_per_channel = samples_per_cpi;
        m.pulse_count = 64;
        m.wave_process_type = integer("wave_process_type");
        m.velocity_oversampling = integer("velocity_oversampling");
        m.sample_rate_hz = number("sample_rate_hz");
        m.nominal_carrier_frequency_hz = number("carrier_frequency_hz");
        m.bandwidth_hz = number("bandwidth_hz");
        m.pulse_width_s = number("pulse_width_s");
        m.nominal_prt_s = number("prt_s");
        m.observation_max_range_m = number("observation_max_range_m");
        m.dequantization_scale = number("input_dequantization_scale");
        if (m.velocity_oversampling == 0 || m.sample_rate_hz <= 0 ||
            m.nominal_carrier_frequency_hz <= 0 || m.bandwidth_hz <= 0 ||
            m.pulse_width_s <= 0 || m.nominal_prt_s <= 0 ||
            m.observation_max_range_m <= 0 || m.dequantization_scale <= 0)
            throw std::runtime_error("non-positive template parameter");
        const auto read_array = [&](const char* name, auto& values) {
            std::ifstream input(directory / name);
            for (auto& value : values)
                if (!(input >> value) || !std::isfinite(value))
                    throw std::runtime_error(std::string("invalid pulse array: ") + name);
            input >> std::ws;
            if (!input.eof()) throw std::runtime_error(std::string("excess pulse array values: ") + name);
        };
        read_array("pulse_time.txt", m.pulse_time_offset_s);
        read_array("pulse_phase.txt", m.pulse_phase_rad);
        read_array("pulse_freq.txt", m.pulse_frequency_hz);
        read_array("wd0.txt", m.coherent_weight);
        for (std::size_t pulse = 0; pulse < 64; ++pulse) {
            if (m.pulse_time_offset_s[pulse] < 0 || m.pulse_frequency_hz[pulse] <= 0 ||
                (pulse && m.pulse_time_offset_s[pulse] < m.pulse_time_offset_s[pulse - 1]))
                throw std::runtime_error("invalid pulse time/frequency");
        }
        if (index) {
            const auto& a = templates[0];
            if (m.wave_process_type != a.wave_process_type ||
                m.velocity_oversampling != a.velocity_oversampling ||
                m.sample_rate_hz != a.sample_rate_hz ||
                m.nominal_carrier_frequency_hz != a.nominal_carrier_frequency_hz ||
                m.bandwidth_hz != a.bandwidth_hz || m.pulse_width_s != a.pulse_width_s ||
                m.nominal_prt_s != a.nominal_prt_s ||
                m.observation_max_range_m != a.observation_max_range_m ||
                m.dequantization_scale != a.dequantization_scale)
                throw std::runtime_error("waveform configuration differs from CPI0");
        }
    }
    return templates;
}
} // namespace pcie_source
