#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cstddef>
#include <cstdint>

namespace kalman_tracker_data = cycore::algorithm::kalman_tracker;

class KalmanTrackerAlgorithm {
public:
    explicit KalmanTrackerAlgorithm(const cycore::sdk::Params&) {}

    bool work(cycore::sdk::Reader<std::byte>& in,
              cycore::sdk::Writer<std::byte>& out) {
        auto plots = in.read_raw_array<kalman_tracker_data::Plot>();
        if (!plots) {
            return false;
        }

        auto tracks = out.reserve_raw_array<kalman_tracker_data::Track>(plots->size());
        if (!tracks) {
            return false;
        }

        for (std::size_t i = 0; i < plots->size(); ++i) {
            const auto& plot = (*plots)[i];
            (*tracks)[i] = kalman_tracker_data::Track{
                static_cast<std::uint32_t>(i + 1),
                1U,
                plot.range,
                plot.velocity,
                plot.power};
        }
        return true;
    }
};
