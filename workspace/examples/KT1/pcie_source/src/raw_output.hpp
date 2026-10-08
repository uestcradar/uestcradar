#pragma once
#include "raw_iq.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

namespace pcie_source {
// One acquisition producer, one SDK writer. Capacity includes the in-flight block.
class RawOutput {
public:
    explicit RawOutput(std::size_t capacity)
        : output_(std::chrono::seconds(3)), capacity_(capacity) {
        if (!capacity || capacity > 32768) throw std::invalid_argument("queue-frames must be 1..32768");
        worker_ = std::thread([this] { run(); });
    }
    ~RawOutput() {
        abort_.store(true);
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
    RawOutput(const RawOutput&) = delete;
    RawOutput& operator=(const RawOutput&) = delete;

    void push(raw_iq::Block block) {
        if (block.metadata.channel_count != 1 || !block.metadata.samples_per_channel ||
            block.metadata.samples_per_channel > 8192 || block.samples.size() != block.metadata.samples_per_channel)
            throw std::invalid_argument("output requires a bounded single-channel IQ frame");
        std::lock_guard lock(mutex_);
        if (error_) std::rethrow_exception(error_);
        if (closing_) throw std::runtime_error("output queue is closed");
        if (buffered_ == capacity_) throw std::runtime_error("raw IQ output queue overflow; recording run failed");
        queue_.push_back(std::move(block));
        ++buffered_;
        ready_.notify_one();
    }
    void check() {
        if (!failed_.load(std::memory_order_acquire)) return;
        std::lock_guard lock(mutex_);
        std::rethrow_exception(error_);
    }
    raw_iq::Digest finish() {
        {
            std::lock_guard lock(mutex_);
            closing_ = true;
            drain_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
        check();
        return digest_;
    }
    std::uint64_t sent() const { return sent_.load(); }
private:
    void run() noexcept {
        try {
            for (;;) {
                raw_iq::Block block;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait(lock, [&] { return abort_.load() || closing_ || !queue_.empty(); });
                    if (abort_.load() || queue_.empty()) return;
                    block = std::move(queue_.front());
                    queue_.pop_front();
                }
                const auto stalled_since = std::chrono::steady_clock::now();
                std::optional<uestcradar::RawIQFrame> frame;
                while (!frame) {
                    if (abort_.load()) return;
                    const auto now = std::chrono::steady_clock::now();
                    {
                        std::lock_guard lock(mutex_);
                        if ((closing_ && now >= drain_deadline_) || now - stalled_since >= std::chrono::seconds(5))
                            throw std::runtime_error("raw IQ output drain timed out");
                    }
                    frame = output_.try_create(block.metadata);
                    if (!frame) std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
                std::copy(block.samples.begin(), block.samples.end(), frame->data().values().begin());
                output_.write(std::move(*frame));
                digest_.add(block.metadata, block.samples);
                sent_.store(digest_.frames);
                std::lock_guard lock(mutex_);
                --buffered_;
            }
        } catch (...) {
            std::lock_guard lock(mutex_);
            error_ = std::current_exception();
            failed_.store(true, std::memory_order_release);
        }
    }
    uestcradar::Output<uestcradar::RawIQFrame> output_;
    const std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<raw_iq::Block> queue_;
    std::size_t buffered_{};
    bool closing_{};
    std::chrono::steady_clock::time_point drain_deadline_;
    std::exception_ptr error_;
    std::atomic<bool> abort_{false}, failed_{false};
    std::atomic<std::uint64_t> sent_{0};
    raw_iq::Digest digest_;
    std::thread worker_;
};
} // namespace pcie_source
