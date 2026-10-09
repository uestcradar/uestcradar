#pragma once
#include <array>
#include <charconv>
#include <optional>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace pcie_source {
using CpuMap = std::array<int, 3>;

inline CpuMap parse_cpu_map(std::string_view text) {
    CpuMap cpus{};
    for (std::size_t i = 0; i < cpus.size(); ++i) {
        const auto comma = text.find(',');
        const auto field = text.substr(0, comma);
        const auto parsed = std::from_chars(field.data(), field.data() + field.size(), cpus[i]);
        if (field.empty() || parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size() ||
            cpus[i] < 0 || cpus[i] >= CPU_SETSIZE ||
            (i < 2 ? comma == std::string_view::npos : comma != std::string_view::npos))
            throw std::invalid_argument("cpu-map requires three CPU IDs: RX,DMA,OUTPUT");
        if (i < 2) text.remove_prefix(comma + 1);
    }
    if (cpus[0] == cpus[1] || cpus[0] == cpus[2] || cpus[1] == cpus[2])
        throw std::invalid_argument("cpu-map roles require distinct CPUs");
    return cpus;
}

// Validate before narrowing the main thread mask: new threads inherit that mask.
inline void validate_cpu_map(const CpuMap& cpus, bool capture_only) {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    const int error = pthread_getaffinity_np(pthread_self(), sizeof(allowed), &allowed);
    if (error) throw std::system_error(error, std::generic_category(), "read allowed CPUs");
    for (std::size_t i = 0; i < (capture_only ? 2U : 3U); ++i) {
        if (cpus[i] < 0 || cpus[i] >= CPU_SETSIZE || !CPU_ISSET(cpus[i], &allowed))
            throw std::invalid_argument("cpu-map CPU is offline or outside the allowed set");
        for (std::size_t j = 0; j < i; ++j)
            if (cpus[i] == cpus[j]) throw std::invalid_argument("cpu-map roles require distinct CPUs");
    }
}

inline void bind_cpu(pthread_t thread, int cpu) {
    if (cpu < 0 || cpu >= CPU_SETSIZE) throw std::invalid_argument("invalid CPU ID");
    cpu_set_t requested, actual;
    CPU_ZERO(&requested);
    CPU_ZERO(&actual);
    CPU_SET(cpu, &requested);
    int error = pthread_setaffinity_np(thread, sizeof(requested), &requested);
    if (error) throw std::system_error(error, std::generic_category(), "set thread CPU affinity");
    error = pthread_getaffinity_np(thread, sizeof(actual), &actual);
    if (error) throw std::system_error(error, std::generic_category(), "verify thread CPU affinity");
    if (!CPU_EQUAL(&requested, &actual)) throw std::runtime_error("thread CPU affinity readback differs");
}
} // namespace pcie_source
