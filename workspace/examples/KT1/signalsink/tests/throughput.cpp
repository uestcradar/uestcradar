// Controlled IQ producer for the isolated SDK -> SignalSink disk benchmark.
// Not a hardware acquisition source, RDMA test, or production Worker.
#include <data.h>
#include "ringbuf/ringbuf.hpp"
#include <algorithm>
#include <charconv>
#include <bit>
#include <vector>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace { volatile std::sig_atomic_t stopping = 0; void stop(int) { stopping = 1; } }
int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: throughput RING_NAME FRAME_COUNT\n"; return 2; }
    std::uint64_t count{};
    const std::string text = argv[2];
    auto [end, error] = std::from_chars(text.data(), text.data()+text.size(),count);
    if (error != std::errc{} || end != text.data()+text.size() || !count || count > 1000000) return 2;
    std::signal(SIGTERM,stop); std::signal(SIGINT,stop);
    auto* ring = ringbuf_create(argv[1], {4, 2*1024*1024, 1, 3});
    ::setenv("UESTCRADAR_DOWNSTREAM_SHM_NAME",argv[1],1);
    int result = 0;
    try {
        uestcradar::Output<uestcradar::IQFrame> output;
        std::vector<uestcradar::ComplexInt16> pattern(8 * 32768);
        std::uint32_t random = 0x12345678;
        for (auto& sample : pattern) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            sample = {std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(random)),
                      std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(random >> 16))};
        }
        std::cout << "ready" << std::endl;
        std::string command;
        if (!std::getline(std::cin,command) || command != "go") throw std::runtime_error("expected go");
        uestcradar::IQMetadata metadata{};
        metadata.channel_count=8; metadata.samples_per_channel=32768; metadata.pulse_count=1;
        metadata.sample_rate_hz=15360000; metadata.nominal_carrier_frequency_hz=1.2e9;
        metadata.nominal_prt_s=32768.0/15360000; metadata.dequantization_scale=1;
        metadata.coherent_weight[0]=1;
        const auto started = std::chrono::steady_clock::now();
        std::uint64_t sent=0;
        while (sent<count && !stopping) {
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
            while (ringbuf_occupied_slots(ring)==4 && !stopping) {
                if (std::chrono::steady_clock::now()>deadline) throw std::runtime_error("consumer stalled");
                std::this_thread::sleep_for(std::chrono::microseconds{50});
            }
            if (stopping) break;
            metadata.cpi_index=sent;
            auto frame=output.create(metadata);
            const auto sample=static_cast<std::int16_t>(sent % 32768);
            auto values = frame.data().values();
            std::copy(pattern.begin(), pattern.end(), values.begin());
            values.front() = {sample,static_cast<std::int16_t>(-sample)};
            output.write(std::move(frame));
            ++sent;
            const auto target = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>{static_cast<double>(sent)/468.75});
            std::this_thread::sleep_until(started+target);
        }
        const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        std::cout << "{\"frames\":" << sent << ",\"payload_bytes\":" << sent*1048576ULL << ",\"seconds\":" << seconds << "}" << std::endl;
        // Keep the input Ring alive until the recorder has finalized.
        std::getline(std::cin,command);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result=1; }
    ringbuf_shutdown(ring); ringbuf_close(ring); ringbuf_unlink(argv[1]);
    return result;
}
