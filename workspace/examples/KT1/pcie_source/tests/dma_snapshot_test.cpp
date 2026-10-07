#include "dma_snapshot.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

int main() {
    pcie_source::DmaSnapshot copy;
    copy.copy({}, {}, {});
    std::vector<std::byte> source(65552), first(65552), second(65552);
    constexpr std::array<std::size_t, 8> lengths{8, 264, 4095, 4096, 4104, 32768, 65528, 65536};
    for (unsigned round = 0; round < 200; ++round) {
        const auto bytes = lengths[round % lengths.size()];
        for (std::size_t i = 0; i < source.size(); ++i) source[i] = std::byte((i + round) & 255);
        std::fill(first.begin(), first.end(), std::byte{0x5a});
        std::fill(second.begin(), second.end(), std::byte{0xa5});
        const auto src = std::span(source).subspan(1, bytes); // Unaligned host memory is supported.
        copy.copy(std::span(first).subspan(4, bytes), std::span(second).subspan(8, bytes), src);
        assert(std::memcmp(first.data() + 4, src.data(), bytes) == 0);
        assert(std::memcmp(second.data() + 8, src.data(), bytes) == 0);
        for (std::size_t i = 0; i < first.size(); ++i) {
            if (i < 4 || i >= 4 + bytes) assert(first[i] == std::byte{0x5a});
            if (i < 8 || i >= 8 + bytes) assert(second[i] == std::byte{0xa5});
        }
    }
    bool rejected = false;
    try { copy.copy(std::span(first).first(8), second, source); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    copy.stop();
    copy.stop(); // Idempotent explicit join before DMA unmapping.
    rejected = false;
    try { copy.copy(first, second, source); }
    catch (const std::logic_error&) { rejected = true; }
    assert(rejected);
}
