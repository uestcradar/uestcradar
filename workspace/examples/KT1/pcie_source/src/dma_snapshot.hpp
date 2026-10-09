#pragma once
#include "cpu_affinity.hpp"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <thread>

namespace pcie_source {
// One caller, one outstanding packet. No queued DMA pointers; copy() joins both halves.
// Two snapshots remain diagnostics, NOT a hardware ownership/consistency guarantee.
class DmaSnapshot {
public:
    explicit DmaSnapshot(std::optional<int> cpu = std::nullopt) : worker_([this] { run(); }) {
        // The helper waits for work; no DMA access is possible before binding succeeds.
        try { if (cpu) bind_cpu(worker_.native_handle(), *cpu); }
        catch (...) { stop(); throw; }
    }
    ~DmaSnapshot() { stop(); }
    DmaSnapshot(const DmaSnapshot&) = delete;
    DmaSnapshot& operator=(const DmaSnapshot&) = delete;

    void stop() noexcept {
        if (!worker_.joinable()) return;
        state_.store(State::stop, std::memory_order_release);
        state_.notify_one();
        worker_.join();
    }

    void copy(std::span<std::byte> first, std::span<std::byte> second,
              std::span<const std::byte> source) {
        if (first.size() != source.size() || second.size() != source.size())
            throw std::invalid_argument("DMA snapshot sizes differ");
        if (source.empty()) return;
        if (!source.data() || !first.data() || !second.data())
            throw std::invalid_argument("null DMA snapshot buffer");
        if (!worker_.joinable()) throw std::logic_error("DMA snapshot worker stopped");
        if (source.size() < 4096) {
            copy_twice(first.data(), second.data(), source.data(), source.size());
            return;
        }
        // Split on complete 8-byte sample groups. Output buffers must be distinct/non-overlapping.
        const auto split = (source.size() / 2) & ~std::size_t{7};
        first_ = first.data() + split;
        second_ = second.data() + split;
        source_ = source.data() + split;
        bytes_ = source.size() - split;
        state_.store(State::work, std::memory_order_release);
        state_.notify_one();
        copy_twice(first.data(), second.data(), source.data(), split);
        while (state_.load(std::memory_order_acquire) == State::work)
            state_.wait(State::work, std::memory_order_acquire);
        // Worker no longer accesses this packet; caller can compare/reuse buffers safely.
    }

private:
    enum class State : unsigned { idle, work, stop };
    static void copy_twice(std::byte* first, std::byte* second,
                           const std::byte* source, std::size_t bytes) {
        std::memcpy(first, source, bytes);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        std::memcpy(second, source, bytes);
    }
    void run() {
        for (;;) {
            state_.wait(State::idle, std::memory_order_acquire);
            if (state_.load(std::memory_order_acquire) == State::stop) return;
            copy_twice(first_, second_, source_, bytes_);
            state_.store(State::idle, std::memory_order_release);
            state_.notify_one();
        }
    }
    std::atomic<State> state_{State::idle};
    std::byte *first_{}, *second_{};
    const std::byte* source_{};
    std::size_t bytes_{};
    std::thread worker_;
};
} // namespace pcie_source
