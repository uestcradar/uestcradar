#pragma once
#include "algorithm_process.h"
#include "rd_image.h"
#include <optional>
namespace radar_qt_example {
class WorkerSupport {
public:
    WorkerSupport();
    void start_algorithm() { algorithm_.start(); }
    bool running() const { return limit_==0 || completed_<limit_; }
    CpiBuffer receive_cpi();
    void write_algorithm_input(const QByteArray& bytes) { algorithm_.write(bytes); }
    AlgorithmResult wait_algorithm_result() { return algorithm_.wait_result(); }
    void publish_rd(const RdOutput& result, const CpiBuffer& cpi);
    void stop_algorithm() { algorithm_.stop(); }
private:
    uestcradar::Input<uestcradar::PulseCompressionFrame> input_;
    uestcradar::Output<uestcradar::RDFrame> output_;
    std::optional<uestcradar::PulseCompressionFrame> parent_;
    AlgorithmProcess algorithm_;
    std::uint64_t completed_{};
    std::uint64_t limit_{};
};
}
