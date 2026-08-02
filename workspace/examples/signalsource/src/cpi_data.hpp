#pragma once

#include <data.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <regex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace radar_example {

struct CpiData {
    uestcradar::IQMetadata metadata;
    std::vector<std::byte> cs16;
};

inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), {}};
}

inline double json_number(
    const std::string& json,
    const std::string& key) {
    const std::regex pattern{
        "\\\"" + key +
        "\\\"\\s*:\\s*([-+]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][-+]?[0-9]+)?)"};
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) {
        throw std::runtime_error("metadata.json is missing " + key);
    }
    char* end = nullptr;
    const double value = std::strtod(match[1].str().c_str(), &end);
    if (end == nullptr || *end != '\0') {
        throw std::runtime_error("metadata.json has invalid " + key);
    }
    return value;
}

inline std::string json_string(
    const std::string& json,
    const std::string& key) {
    const std::regex pattern{
        "\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\""};
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) {
        throw std::runtime_error("metadata.json is missing " + key);
    }
    return match[1].str();
}

inline std::uint64_t json_uint64(
    const std::string& json,
    const std::string& key) {
    const std::regex pattern{
        "\\\"" + key + "\\\"\\s*:\\s*([0-9]+)"};
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) {
        throw std::runtime_error("metadata.json is missing integer " + key);
    }
    try {
        return std::stoull(match[1].str());
    } catch (const std::exception&) {
        throw std::runtime_error("metadata.json integer is too large: " + key);
    }
}

inline std::uint32_t json_uint32(
    const std::string& json,
    const std::string& key) {
    const auto value = json_uint64(json, key);
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("metadata.json value is too large: " + key);
    }
    return static_cast<std::uint32_t>(value);
}

inline std::vector<double> read_double_lines(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::vector<double> values;
    double value{};
    while (input >> value) {
        values.push_back(value);
    }
    if (!input.eof()) {
        throw std::runtime_error("invalid floating-point value in " + path.string());
    }
    return values;
}

inline void copy_pulse_parameters(
    std::array<double, uestcradar::kMaxPulsesPerCpi>& destination,
    const std::filesystem::path& path,
    std::uint32_t pulse_count) {
    const auto values = read_double_lines(path);
    if (values.size() != pulse_count) {
        throw std::runtime_error(
            path.string() + " must contain exactly " +
            std::to_string(pulse_count) + " values");
    }
    std::copy(values.begin(), values.end(), destination.begin());
}

inline CpiData load_cpi(const std::filesystem::path& directory) {
    const std::string json = read_text_file(directory / "metadata.json");
    if (json_uint32(json, "format_version") != 2 ||
        json_string(json, "input_layout") !=
            "CS16 little-endian: int16 I,int16 Q,I,Q,..." ||
        json_uint32(json, "input_sample_bytes") != 4) {
        throw std::runtime_error("CPI input format is not little-endian CS16");
    }
    const std::uint32_t sample_count = json_uint32(json, "sample_count");
    const std::uint32_t pulse_count = json_uint32(json, "pulse_n");
    if (sample_count == 0 || pulse_count == 0 ||
        pulse_count > uestcradar::kMaxPulsesPerCpi) {
        throw std::runtime_error("CPI dimensions are invalid");
    }

    CpiData result;
    result.metadata = {
        .cpi_index = json_uint64(json, "cpi_index"),
        .channel_count = 1,
        .samples_per_channel = sample_count,
        .pulse_count = pulse_count,
        .wave_process_type = json_uint32(json, "wave_process_type"),
        .velocity_oversampling = json_uint32(json, "velocity_oversampling"),
        .sample_rate_hz = json_number(json, "sample_rate_hz"),
        .nominal_carrier_frequency_hz =
            json_number(json, "carrier_frequency_hz"),
        .bandwidth_hz = json_number(json, "bandwidth_hz"),
        .pulse_width_s = json_number(json, "pulse_width_s"),
        .nominal_prt_s = json_number(json, "prt_s"),
        .observation_max_range_m =
            json_number(json, "observation_max_range_m"),
        .dequantization_scale =
            json_number(json, "input_dequantization_scale"),
    };
    copy_pulse_parameters(
        result.metadata.pulse_time_offset_s,
        directory / "pulse_time.txt",
        pulse_count);
    copy_pulse_parameters(
        result.metadata.pulse_phase_rad,
        directory / "pulse_phase.txt",
        pulse_count);
    copy_pulse_parameters(
        result.metadata.pulse_frequency_hz,
        directory / "pulse_freq.txt",
        pulse_count);
    copy_pulse_parameters(
        result.metadata.coherent_weight,
        directory / "wd0.txt",
        pulse_count);

    if (sample_count > std::numeric_limits<std::size_t>::max() /
            sizeof(uestcradar::ComplexInt16)) {
        throw std::runtime_error("CPI input size overflows");
    }
    const std::size_t expected_bytes =
        static_cast<std::size_t>(sample_count) *
        sizeof(uestcradar::ComplexInt16);
    result.cs16.resize(expected_bytes);
    const auto input_path = directory / "input.bin";
    std::ifstream input(input_path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 ||
        static_cast<std::uint64_t>(input.tellg()) != expected_bytes) {
        throw std::runtime_error(
            input_path.string() + " size does not equal sample_count * 4");
    }
    input.seekg(0);
    input.read(
        reinterpret_cast<char*>(result.cs16.data()),
        static_cast<std::streamsize>(result.cs16.size()));
    if (!input) {
        throw std::runtime_error("cannot read " + input_path.string());
    }
    static_assert(std::is_trivially_copyable_v<uestcradar::ComplexInt16>);
    static_assert(sizeof(uestcradar::ComplexInt16) == 4);
    return result;
}

inline void copy_cpi_samples(
    const CpiData& cpi,
    uestcradar::IQFrame& frame) {
    auto values = frame.data().values();
    const std::size_t bytes = values.size_bytes();
    if (bytes != cpi.cs16.size()) {
        throw std::invalid_argument("IQFrame shape does not match CPI data");
    }
    std::memcpy(values.data(), cpi.cs16.data(), bytes);
}

}  // namespace radar_example
