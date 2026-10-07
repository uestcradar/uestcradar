#include "cpi_assembler.hpp"
#include "options.hpp"
#include "pcie_receiver.hpp"
#include <chrono>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void stop(int) { stop_requested = 1; }

void log_templates(const pcie_source::Templates& templates) {
    for (const auto& m : templates) {
        std::cout << "[template] index=" << m.cpi_index
            << " channel_count=" << m.channel_count << " samples_per_channel=" << m.samples_per_channel
            << " pulse_count=" << m.pulse_count << " wave_process_type=" << m.wave_process_type
            << " velocity_oversampling=" << m.velocity_oversampling
            << " sample_rate_hz=" << m.sample_rate_hz
            << " nominal_carrier_frequency_hz=" << m.nominal_carrier_frequency_hz
            << " bandwidth_hz=" << m.bandwidth_hz << " pulse_width_s=" << m.pulse_width_s
            << " nominal_prt_s=" << m.nominal_prt_s
            << " observation_max_range_m=" << m.observation_max_range_m
            << " dequantization_scale=" << m.dequantization_scale << '\n';
        const auto log = [&](const char* name, const auto& values) {
            std::cout << "[template] index=" << m.cpi_index << ' ' << name << '=';
            for (const auto value : values) std::cout << value << ',';
            std::cout << '\n';
        };
        log("pulse_time_offset_s", m.pulse_time_offset_s);
        log("pulse_phase_rad", m.pulse_phase_rad);
        log("pulse_frequency_hz", m.pulse_frequency_hz);
        log("coherent_weight", m.coherent_weight);
    }
}
}

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf << std::setprecision(17);
        const auto options = pcie_source::parse_options(argc, argv);
        if (options.help) {
            std::cout << "Usage: pcie_source --capture-only [--channel 0..7] "
                "[--duration-seconds N] [--data-root PATH] [--pcie-config-dir PATH] [--timestamp-errors PATH]\n"
                "RX timestamp check is always enabled (delta=" << pcie_source::TimestampCheck::expected_delta
                << "); errors append to rx_timestamp_errors.jsonl by default.\n"
                "Capture-only is diagnostic: no SDK/SHM connection, integrity_verified=false.\n"
                "Normal output is blocked pending DMA continuity and SDK cancellation validation.\n";
            return 0;
        }
        if (!options.capture_only)
            throw std::runtime_error("normal output is not implemented: DMA continuity and SDK cancellation unresolved; use --capture-only for diagnostics");
        const auto templates = pcie_source::load_templates(options.data_root);
        log_templates(templates);
        struct sigaction action{};
        action.sa_handler = stop;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, nullptr) || sigaction(SIGTERM, &action, nullptr))
            throw std::system_error(errno, std::generic_category(), "install signal handlers");
        std::cout << "[source] mode=capture-only synthetic_metadata=true integrity_verified=false"
            << " channel=" << options.channel << " data_root=" << options.data_root
            << " pcie_config_dir=" << options.config_dir
            << " timestamp_errors=" << options.timestamp_errors << '\n';
        // ponytail: buffered synchronous logging; use a bounded disk-writer queue if measured I/O stalls RX.
        std::ofstream timestamp_errors;
        timestamp_errors.exceptions(std::ios::failbit | std::ios::badbit);
        timestamp_errors.open(options.timestamp_errors, std::ios::app);
        const auto run_id = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        timestamp_errors << "{\"event\":\"run_start\",\"run_id\":\"" << run_id
            << "\",\"channel\":" << options.channel << ",\"expected_delta\":"
            << pcie_source::TimestampCheck::expected_delta << "}\n";
        timestamp_errors.flush(); // Detect an unusable log destination before touching hardware.
        pcie_source::CpiAssembler assembler(templates);
        pcie_source::PcieReceiver receiver(options.config_dir);
        const auto start = std::chrono::steady_clock::now();
        auto last_log = start;
        std::uint64_t discarded_partial_samples = 0, discarded_partial_blocks = 0;
        const auto discard_partial = [&] {
            const auto count = assembler.discard_partial();
            discarded_partial_samples += count;
            if (count) ++discarded_partial_blocks;
        };
        const auto report = [&] {
            const auto& s = receiver.stats();
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << "[capture] elapsed_s=" << elapsed << " synchronized=" << receiver.synchronized()
                << " descriptors=" << s.descriptors << " iq_packets=" << s.iq_packets
                << " iq_bytes=" << s.iq_bytes << " control_packets=" << s.control_packets
                << " bit_packets=" << s.bit_packets << " invalid_packets=" << s.invalid_packets
                << " changed_copies=" << s.changed_copies
                << " rx_timestamp=" << (s.timestamp_check.previous ?
                    std::to_string(s.timestamp_check.previous->timestamps.rx_first) : "none")
                << " timestamp_comparisons=" << s.timestamp_check.comparisons
                << " timestamp_errors=" << s.timestamp_check.errors
                << " completed_cpi=" << assembler.completed()
                << " diagnostic_released_cpi=" << assembler.completed()
                << " discarded_partial_blocks=" << discarded_partial_blocks
                << " discarded_partial_samples=" << discarded_partial_samples
                << " channel=" << options.channel << " channel_samples=";
            for (const auto count : s.channel_samples) std::cout << count << ',';
            std::cout << " integrity_verified=false hardware_loss_count=unknown\n";
            timestamp_errors.flush();
        };
        while (!stop_requested) {
            const auto now = std::chrono::steady_clock::now();
            if (options.duration_seconds && now - start >= std::chrono::seconds(options.duration_seconds)) break;
            const auto descriptors_before = receiver.stats().descriptors;
            auto block = receiver.poll(options.channel);
            if (block.timestamp_error)
                pcie_source::write_timestamp_error(timestamp_errors, run_id, options.channel, *block.timestamp_error);
            if (block.gap) discard_partial();
            if (!block.samples.empty()) assembler.append(block.samples, [](pcie_source::Cpi) {});
            else if (receiver.stats().descriptors == descriptors_before)
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            if (now - last_log >= std::chrono::seconds(1)) { report(); last_log = now; }
        }
        discard_partial();
        report();
        const auto& stats = receiver.stats();
        const auto& check = stats.timestamp_check;
        const bool continuous = check.comparisons > 0 && check.errors == 0 && stats.invalid_packets == 0;
        timestamp_errors << "{\"event\":\"run_end\",\"run_id\":\"" << run_id
            << "\",\"observations\":\"" << check.observations << "\",\"comparisons\":\"" << check.comparisons
            << "\",\"errors\":\"" << check.errors << "\"}\n";
        timestamp_errors.close();
        std::cout << "[timestamp-check] pipe=0 observations=" << check.observations
            << " comparisons=" << check.comparisons << " consecutive=" << check.consecutive
            << " errors=" << check.errors << " wraps=" << check.wraps
            << " expected_delta=" << pcie_source::TimestampCheck::expected_delta
            << " strict_continuous=" << (continuous ? "true" : "false")
            << " errors_file=" << options.timestamp_errors << " modifies_iq=false\n";
        const bool observed = stats.iq_packets && assembler.completed();
        const bool errors = stats.invalid_packets || stats.changed_copies;
        std::cout << "[result] data_present=" << (observed ? "true" : "false")
            << " receive_errors=" << (errors ? "true" : "false")
            << " integrity_verified=false downstream_tested=false\n";
        return observed && !errors ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "[source] error=" << error.what() << '\n';
        return 1;
    }
}
