#include "raw_assembler.hpp"
#include "raw_output.hpp"
#include "options.hpp"
#include "pcie_receiver.hpp"
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <system_error>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void stop(int) { stop_requested = 1; }
}

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf << std::setprecision(17);
        const auto options = pcie_source::parse_options(argc, argv);
        if (options.help) {
            std::cout << "Usage: pcie_source [--capture-only] [--channel 0..7] [--queue-frames 1..32768] "
                "[--duration-seconds N] [--pcie-config-dir PATH] [--timestamp-errors PATH]\n"
                "Output: RawIQFrame 4:1, one selected channel, no CPI templates.\n"
                "Capture-only: same framing checks, no SDK/SHM output.\n"
                "RX timestamp diagnostics retain raw integers; DMA ownership remains unverified.\n";
            return 0;
        }
        struct sigaction action{};
        action.sa_handler = stop;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, nullptr) || sigaction(SIGTERM, &action, nullptr))
            throw std::system_error(errno, std::generic_category(), "install signal handlers");
        // Validate log and SDK port before the first hardware access.
        std::ofstream timestamp_errors;
        timestamp_errors.exceptions(std::ios::failbit | std::ios::badbit);
        timestamp_errors.open(options.timestamp_errors, std::ios::app);
        const auto run_id = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        timestamp_errors << "{\"event\":\"run_start\",\"run_id\":\"" << run_id
            << "\",\"channel\":" << options.channel << ",\"expected_delta\":"
            << pcie_source::TimestampCheck::expected_delta << "}\n";
        timestamp_errors.flush();
        std::unique_ptr<pcie_source::RawOutput> output;
        if (!options.capture_only) output = std::make_unique<pcie_source::RawOutput>(options.queue_frames);
        if (stop_requested) return 0; // Do not initialize hardware after a stop during port startup.
        std::cout << "[source] mode=" << (options.capture_only ? "capture-only" : "raw-iq-output")
            << " contract=4:1 selected_channel=" << options.channel << " channel_count=1 templates=disabled"
            << " queue_frames=" << options.queue_frames << " integrity_verified=false\n";
        pcie_source::RawAssembler assembler;
        pcie_source::PcieReceiver receiver(options.config_dir);
        const auto start = std::chrono::steady_clock::now();
        auto last_log = start;
        raw_iq::Digest digest;
        const auto report = [&] {
            const auto& s = receiver.stats();
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << "[capture] elapsed_s=" << elapsed << " synchronized=" << receiver.synchronized()
                << " descriptors=" << s.descriptors << " iq_packets=" << s.iq_packets
                << " control_packets=" << s.control_packets << " bit_packets=" << s.bit_packets
                << " invalid_packets=" << s.invalid_packets << " changed_copies=" << s.changed_copies
                << " timestamp_comparisons=" << s.timestamp_check.comparisons
                << " timestamp_errors=" << s.timestamp_check.errors
                << " selected_channel=" << options.channel << " channel_count=1"
                << " selected_samples=" << s.channel_samples[options.channel]
                << " completed_frames=" << assembler.completed()
                << " submitted_frames=" << (output ? output->sent() : 0)
                << " startup_samples=" << assembler.startup_samples()
                << " tail_samples=" << assembler.partial_samples()
                << " integrity_verified=false hardware_loss_count=unknown\n";
            timestamp_errors.flush();
        };
        std::exception_ptr failure;
        try {
            while (!stop_requested) {
                const auto now = std::chrono::steady_clock::now();
                if (options.duration_seconds && now - start >= std::chrono::seconds(options.duration_seconds)) break;
                if (output) output->check();
                const auto before = receiver.stats().descriptors;
                auto block = receiver.poll(options.channel);
                if (block.timestamp_error)
                    pcie_source::write_timestamp_error(timestamp_errors, run_id, options.channel, *block.timestamp_error);
                if (block.gap) throw std::runtime_error("PCIe receive gap or unstable DMA snapshot; recording run failed");
                if (block.control) assembler.control(*block.control);
                if (auto frame = assembler.append(block.samples)) {
                    if (output) output->push(std::move(*frame));
                    else { ++digest.frames; digest.samples += frame->samples.size(); }
                }
                if (receiver.stats().descriptors == before)
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                if (now - last_log >= std::chrono::seconds(1)) { report(); last_log = now; }
            }
        } catch (...) { failure = std::current_exception(); }
        try { if (output) digest = output->finish(); }
        catch (...) { if (!failure) failure = std::current_exception(); }
        report();
        const auto& s = receiver.stats();
        timestamp_errors << "{\"event\":\"run_end\",\"run_id\":\"" << run_id
            << "\",\"observations\":\"" << s.timestamp_check.observations
            << "\",\"comparisons\":\"" << s.timestamp_check.comparisons
            << "\",\"errors\":\"" << s.timestamp_check.errors << "\"}\n";
        timestamp_errors.close();
        if (failure) std::rethrow_exception(failure);
        const bool success = digest.frames > 0 && digest.frames == assembler.completed() &&
            s.timestamp_check.comparisons > 0 && !s.timestamp_check.errors && !s.invalid_packets && !s.changed_copies;
        std::cout << "[raw-iq] synthetic=false capture_only=" << options.capture_only
            << " channel_count=1 selected_channel=" << options.channel
            << " frames=" << digest.frames << " samples=" << digest.samples;
        if (output) std::cout << " business_fnv1a64=" << digest.value;
        std::cout << " receive_check=" << (success ? "pass" : "fail")
            << " integrity_verified=false downstream_tested=false\n";
        return success ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "[source] error=" << error.what() << '\n';
        return 1;
    }
}
