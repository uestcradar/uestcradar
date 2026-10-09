#include "options.hpp"
#include <cassert>
#include <vector>

int main() {
    const auto parse = [](std::initializer_list<const char*> args) {
        std::vector<char*> argv;
        for (auto arg : args) argv.push_back(const_cast<char*>(arg));
        return pcie_source::parse_options(static_cast<int>(argv.size()), argv.data());
    };
    unsetenv("PCIE_CPU_MAP");
    const auto defaults = parse({"pcie_source"});
    assert((defaults.cpus == pcie_source::CpuMap{8,9,10}));
    setenv("PCIE_CPU_MAP", "11,12,13", 1);
    assert((parse({"pcie_source"}).cpus == pcie_source::CpuMap{11,12,13}));
    setenv("PCIE_CPU_MAP", "invalid", 1);
    assert((parse({"pcie_source", "--cpu-map", "1,2,3"}).cpus == pcie_source::CpuMap{1,2,3}));
    unsetenv("PCIE_CPU_MAP");
    assert(defaults.channel == 0 && defaults.queue_frames == 512 && !defaults.capture_only);
    assert(defaults.timestamp_errors == "rx_timestamp_errors.jsonl");
    const auto capture = parse({"pcie_source", "--capture-only", "--channel", "7",
        "--duration-seconds", "30", "--queue-frames", "64", "--timestamp-errors", "/tmp/errors.jsonl"});
    assert(capture.capture_only && capture.channel == 7 && capture.duration_seconds == 30);
    assert(capture.timestamp_errors == "/tmp/errors.jsonl" && capture.queue_frames == 64);
    const auto fails = [&](std::initializer_list<const char*> args) {
        try { (void)parse(args); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    assert(fails({"pcie_source", "--cpu-map", "8,8,10"}));
    assert(fails({"pcie_source", "--cpu-map", "8,9"}));
    setenv("PCIE_CPU_MAP", "invalid", 1);
    assert(fails({"pcie_source"}));
    unsetenv("PCIE_CPU_MAP");
    assert(fails({"pcie_source", "--channel", "-1"}));
    assert(fails({"pcie_source", "--channel", "8"}));
    assert(fails({"pcie_source", "--channel", "1junk"}));
    assert(fails({"pcie_source", "--channel"}));
    assert(fails({"pcie_source", "--duration-seconds", "4294967296"}));
    assert(fails({"pcie_source", "--frames", "1"}));
    assert(fails({"pcie_source", "--timestamp-errors"}));
    assert(fails({"pcie_source", "--timestamp-errors", ""}));
    assert(fails({"pcie_source", "--check-frame-sequence"}));
    assert(fails({"pcie_source", "--data-root", "/data"}));
    assert(fails({"pcie_source", "--queue-frames", "0"}));
    assert(fails({"pcie_source", "--queue-frames", "32769"}));
}
