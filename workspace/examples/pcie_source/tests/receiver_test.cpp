#include "xdma_rx.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>

int main() {
    PcieDescriptor d{};
    assert(pcie_parse_descriptor(UINT64_MAX, &d) == 0);
    const std::uint64_t valid = (UINT64_C(15) << 56) | (UINT64_C(3) << 28) | 4096;
    assert(pcie_parse_descriptor(valid, &d) == 1);
    assert(d.offset == 6144 && d.bytes == 4096 && d.mask == 15 && !d.control);
    assert(pcie_parse_descriptor(valid | (UINT64_C(1) << 50), &d) == 1 && d.control);
    assert(pcie_parse_descriptor(UINT64_C(15) << 52, &d) == 1 && d.group == 15);
    assert(pcie_parse_descriptor((UINT64_C(7) << 56) | 8, &d) == -1);
    assert(pcie_parse_descriptor((UINT64_C(15) << 56) | 7, &d) == -1);
    assert(pcie_parse_descriptor((UINT64_C(15) << 56), &d) == -1);
    assert(pcie_parse_descriptor((UINT64_C(15) << 56) |
        (std::uint64_t{PCIE_PIPE_BYTES / 2048} << 28) | 8, &d) == -1);
    assert(pcie_parse_descriptor(valid, nullptr) == -1);

    std::string name = (std::filesystem::temp_directory_path() / "pcie-config-XXXXXX").string();
    const int fd = mkstemp(name.data()); assert(fd >= 0); close(fd);
    PcieConfig config{};
    const auto check = [&](const std::string& text, bool success) {
        { std::ofstream output(name); output << text; }
        assert((pcie_read_config(name.c_str(), &config) == 0) == success);
    };
    check("ABCD1234ABCD1234\n0000000000000001\r\n", true);
    assert(config.count == 2 && config.words[1] == 1);
    check("FFFFFFFFFFFFFFFF", true);
    check("", false);
    check("000000000000000z\n", false);
    check("000000000000001\n", false);
    check("00000000000000001\n", false);
    std::string large;
    for (unsigned i = 0; i <= PCIE_CONFIG_WORDS; ++i) large += "0000000000000001\n";
    check(large, false);
    std::filesystem::remove(name);
}
