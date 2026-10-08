#include "timestamp_check.hpp"
#include "cJSON.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <sstream>

int main() {
    using namespace pcie_source;
    // Unaligned buffer, no valid head/tail: extraction must not validate them.
    std::array<std::byte, 25> storage{};
    auto payload = std::span(storage).subspan(1);
    const auto put64 = [&](std::size_t offset, std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) payload[offset + i] = std::byte((value >> (i * 8)) & 255);
    };
    put64(4, UINT64_MAX);
    put64(12, UINT64_C(0x0102030405060708));
    const auto timestamps = read_control_timestamps(payload.first(20));
    assert(timestamps.tx_start == UINT64_MAX);
    assert(timestamps.rx_first == UINT64_C(0x0102030405060708));
    put64(4, UINT64_C(0x1122334455667788));
    assert(read_control_timestamps(payload).tx_start == UINT64_C(0x1122334455667788));
    for (unsigned length = 0; length < 20; ++length) {
        bool rejected = false;
        try { (void)read_control_timestamps(payload.first(length)); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }

    constexpr std::uint64_t step = 98304; // Protocol requirement, independent of implementation.
    assert(TimestampCheck::expected_delta == step);
    TimestampCheck check;
    const auto observe = [&](std::uint64_t rx) {
        return check.observe({{UINT64_MAX, rx}, check.observations + 1,
                              100 + check.observations, 900, 2048, 24, 15});
    };
    assert(!observe(37)); // Arbitrary first value, not a comparison.
    assert(check.comparisons == 0);
    assert(!observe(37 + step));
    auto error = observe(37 + step);
    assert(error && error->delta == 0);
    error = observe(37 + 4 * step);
    assert(error && error->delta == 3 * step);
    error = observe(12); // Rollback/reset is reported as exact unsigned difference.
    assert(error && error->delta == std::uint64_t(12) - (37 + 4 * step));
    assert(!observe(12 + step)); // Baseline advances even after an error.
    assert(check.observations == 6 && check.comparisons == 5);
    assert(check.consecutive == 2 && check.errors == 3);
    check.previous.reset(); // Unreadable control packet breaks comparison adjacency.
    assert(!observe(UINT64_MAX - (step - 1)));
    assert(!observe(0));
    assert(check.wraps == 1);

    std::ostringstream output;
    for (int i = 0; i < 100; ++i) {
        error = observe(0);
        assert(error);
        write_timestamp_error(output, 123, 7, *error);
    }
    const auto text = output.str();
    assert(std::count(text.begin(), text.end(), '\n') == 100); // No 16-example cap.
    std::istringstream lines(text);
    std::string line;
    for (int i = 0; i < 100; ++i) {
        assert(std::getline(lines, line));
        auto* json = cJSON_Parse(line.c_str());
        assert(json);
        const auto field = [](cJSON* object, const char* name) {
            auto* item = cJSON_GetObjectItemCaseSensitive(object, name);
            assert(cJSON_IsString(item));
            return std::string(item->valuestring);
        };
        assert(field(json, "run_id") == "123" && field(json, "delta_u64") == "0");
        auto* current = cJSON_GetObjectItemCaseSensitive(json, "current");
        auto* previous = cJSON_GetObjectItemCaseSensitive(json, "previous");
        assert(field(current, "tx_start") == "18446744073709551615");
        assert(field(current, "control_packet") == std::to_string(9 + i));
        assert(field(previous, "control_packet") == std::to_string(8 + i));
        assert(cJSON_GetObjectItemCaseSensitive(json, "expected_delta")->valueint == step);
        assert(cJSON_GetObjectItemCaseSensitive(current, "offset")->valueint == 2048);
        assert(cJSON_GetObjectItemCaseSensitive(current, "bytes")->valueint == 24);
        cJSON_Delete(json);
    }
    assert(check.comparisons == check.consecutive + check.errors);
    error = observe(8192); // Old sample-count interval is no longer accepted.
    assert(error && error->delta == 8192);
}
