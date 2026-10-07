#pragma once

#include "cpi_templates.hpp"
#include <algorithm>
#include <limits>
#include <span>
#include <vector>

namespace pcie_source {
struct Cpi {
    uestcradar::IQMetadata metadata;
    std::vector<uestcradar::ComplexInt16> samples;
};

class CpiAssembler {
public:
    explicit CpiAssembler(const Templates& templates) : templates_(templates) {
        samples_.reserve(samples_per_cpi);
    }

    template<class Emit>
    void append(std::span<const uestcradar::ComplexInt16> input, Emit&& emit) {
        while (!input.empty()) {
            const auto count = std::min(input.size(), samples_per_cpi - samples_.size());
            samples_.insert(samples_.end(), input.begin(), input.begin() + count);
            input = input.subspan(count);
            if (samples_.size() == samples_per_cpi) {
                if (next_index_ == std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("software CPI index exhausted");
                auto metadata = templates_[next_index_ % template_count];
                metadata.cpi_index = next_index_++;
                emit(Cpi{metadata, std::move(samples_)});
                samples_.clear();
                samples_.reserve(samples_per_cpi);
            }
        }
    }

    std::size_t discard_partial() {
        const auto count = samples_.size();
        samples_.clear();
        return count;
    }
    std::uint64_t completed() const { return next_index_; }
private:
    const Templates& templates_;
    std::vector<uestcradar::ComplexInt16> samples_;
    std::uint64_t next_index_{};
};
} // namespace pcie_source
