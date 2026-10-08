#include "options.hpp"
#include <cassert>
#include <vector>

int main() {
    const auto parse = [](std::initializer_list<const char*> args) {
        std::vector<char*> argv;
        for (auto arg : args) argv.push_back(const_cast<char*>(arg));
        return pcie_source::parse_options(static_cast<int>(argv.size()), argv.data());
    };
    const auto defaults = parse({"pcie_source"});
    assert(defaults.channel == 0 && defaults.data_root == "/data" && !defaults.capture_only);
    assert(defaults.timestamp_errors == "rx_timestamp_errors.jsonl");
    const auto capture = parse({"pcie_source", "--capture-only", "--channel", "7",
        "--duration-seconds", "30", "--data-root", "/tmp/templates", "--timestamp-errors", "/tmp/errors.jsonl"});
    assert(capture.capture_only && capture.channel == 7 && capture.duration_seconds == 30);
    assert(capture.timestamp_errors == "/tmp/errors.jsonl");
    const auto fails = [&](std::initializer_list<const char*> args) {
        try { (void)parse(args); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    assert(fails({"pcie_source", "--channel", "-1"}));
    assert(fails({"pcie_source", "--channel", "8"}));
    assert(fails({"pcie_source", "--channel", "1junk"}));
    assert(fails({"pcie_source", "--channel"}));
    assert(fails({"pcie_source", "--duration-seconds", "4294967296"}));
    assert(fails({"pcie_source", "--frames", "1"}));
    assert(fails({"pcie_source", "--timestamp-errors"}));
    assert(fails({"pcie_source", "--timestamp-errors", ""}));
    assert(fails({"pcie_source", "--check-frame-sequence"}));
}
