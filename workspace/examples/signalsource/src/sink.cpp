#include <data.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::string type{"pulse"};
    std::uint64_t frames{0};
    std::uint64_t log_every{20};
    std::uint64_t expect_range{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t expect_doppler{
        std::numeric_limits<std::uint64_t>::max()};
};

std::uint64_t parse_uint64(const char* value) {
    char* end = nullptr;
    const auto result = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        throw std::invalid_argument("invalid integer option");
    }
    return result;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--type" && index + 1 < argc) {
            options.type = argv[++index];
        } else if (argument == "--frames" && index + 1 < argc) {
            options.frames = parse_uint64(argv[++index]);
        } else if (argument == "--log-every" && index + 1 < argc) {
            options.log_every = parse_uint64(argv[++index]);
        } else if (argument == "--expect-range" && index + 1 < argc) {
            options.expect_range = parse_uint64(argv[++index]);
        } else if (argument == "--expect-doppler" && index + 1 < argc) {
            options.expect_doppler = parse_uint64(argv[++index]);
        } else {
            throw std::invalid_argument("unknown or incomplete option");
        }
    }
    if ((options.type != "pulse" && options.type != "rd") ||
        options.log_every == 0) {
        throw std::invalid_argument("type or log interval is invalid");
    }
    return options;
}

void consume_pulse(
    uestcradar::RawFrame& raw,
    const Options& options,
    std::uint64_t received) {
    auto pulse = uestcradar::PulseCompressionFrameView::from(raw);
    std::size_t peak_bin = 0;
    float peak = -1.0F;
    const auto bins = pulse.data()[0];
    for (std::size_t index = 0; index < bins.size(); ++index) {
        const float magnitude =
            std::hypot(bins[index].i, bins[index].q);
        if (magnitude > peak) {
            peak = magnitude;
            peak_bin = index;
        }
    }
    if (options.expect_range !=
            std::numeric_limits<std::uint64_t>::max() &&
        peak_bin != options.expect_range) {
        throw std::runtime_error("unexpected pulse peak");
    }
    if (received == 1 || received % options.log_every == 0) {
        std::cout << "[sink] type=pulse received=" << received
                  << " frame=" << raw.envelope().frame_id
                  << " shape=" << pulse.data().rows() << 'x'
                  << pulse.data().columns()
                  << " peak_range_bin=" << peak_bin << '\n';
    }
}

void consume_rd(
    uestcradar::RawFrame& raw,
    const Options& options,
    std::uint64_t received) {
    auto rd = uestcradar::RDFrameView::from(raw);
    std::size_t peak_range = 0;
    std::size_t peak_doppler = 0;
    float peak = -std::numeric_limits<float>::infinity();
    for (std::size_t range = 0; range < rd.data().rows(); ++range) {
        for (std::size_t doppler = 0;
             doppler < rd.data().columns();
             ++doppler) {
            if (rd.data()[range][doppler] > peak) {
                peak = rd.data()[range][doppler];
                peak_range = range;
                peak_doppler = doppler;
            }
        }
    }
    if ((options.expect_range !=
             std::numeric_limits<std::uint64_t>::max() &&
         peak_range != options.expect_range) ||
        (options.expect_doppler !=
             std::numeric_limits<std::uint64_t>::max() &&
         peak_doppler != options.expect_doppler)) {
        throw std::runtime_error("unexpected RD peak");
    }
    if (received == 1 || received % options.log_every == 0) {
        std::cout << "[sink] type=rd received=" << received
                  << " frame=" << raw.envelope().frame_id
                  << " shape=" << rd.data().rows() << 'x'
                  << rd.data().columns()
                  << " peak=" << peak_range << ',' << peak_doppler
                  << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        const Options options = parse_options(argc, argv);
        uestcradar::Input<uestcradar::RawFrame> input;
        std::uint64_t previous_frame_id = 0;

        std::cout << "[sink] waiting for type=" << options.type
                  << " frames=" << options.frames << '\n';

        for (std::uint64_t received = 1;
             options.frames == 0 || received <= options.frames;
             ++received) {
            uestcradar::RawFrame raw = input.read();
            if (raw.envelope().frame_id <= previous_frame_id) {
                throw std::runtime_error("frame_id is not increasing");
            }
            previous_frame_id = raw.envelope().frame_id;

            if (options.type == "pulse") {
                consume_pulse(raw, options, received);
            } else {
                consume_rd(raw, options, received);
            }
        }

        std::cout << "[sink] completed type=" << options.type
                  << " frames=" << options.frames << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[sink] error=" << error.what() << '\n';
        return 1;
    }
}
