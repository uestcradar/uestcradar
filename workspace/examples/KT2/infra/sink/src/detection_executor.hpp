#pragma once

#include "target_validator.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace radar_sink {

struct DetectionResult {
    std::uint64_t sequence{};
    std::uint64_t received{};
    uestcradar::PulseCompressionMetadata metadata{};
    std::size_t rows{};
    std::size_t columns{};
    TargetDetection detection{};
};

class DetectionExecutor {
public:
    DetectionExecutor(
        std::size_t worker_count,
        std::size_t queue_depth,
        TargetConfig config)
        : config_(config), queue_depth_(queue_depth) {
        if (worker_count == 0) {
            throw std::invalid_argument("worker count must be positive");
        }
        if (queue_depth < worker_count) {
            throw std::invalid_argument(
                "queue depth must be at least the worker count");
        }
        for (std::size_t index = 0; index < queue_depth_; ++index) {
            free_jobs_.push_back(std::make_unique<Job>());
        }
        workers_.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    DetectionExecutor(const DetectionExecutor&) = delete;
    DetectionExecutor& operator=(const DetectionExecutor&) = delete;

    ~DetectionExecutor() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        jobs_ready_.notify_all();
        free_ready_.notify_all();
        results_ready_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void submit(
        std::uint64_t sequence,
        std::uint64_t received,
        const uestcradar::PulseCompressionMetadata& metadata,
        std::size_t rows,
        std::size_t columns,
        std::span<const uestcradar::ComplexFloat32> samples) {
        std::unique_ptr<Job> job;
        {
            std::unique_lock lock(mutex_);
            free_ready_.wait(lock, [this] {
                return stopping_ || !free_jobs_.empty();
            });
            if (stopping_) {
                throw std::runtime_error("detection executor is stopping");
            }
            job = std::move(free_jobs_.front());
            free_jobs_.pop_front();
        }

        job->result = {
            .sequence = sequence,
            .received = received,
            .metadata = metadata,
            .rows = rows,
            .columns = columns,
        };
        job->samples.assign(samples.begin(), samples.end());
        job->error = nullptr;

        {
            std::lock_guard lock(mutex_);
            pending_jobs_.push_back(std::move(job));
        }
        jobs_ready_.notify_one();
    }

    DetectionResult take(std::uint64_t sequence) {
        std::unique_ptr<Job> job;
        {
            std::unique_lock lock(mutex_);
            results_ready_.wait(lock, [this, sequence] {
                return stopping_ || completed_jobs_.contains(sequence);
            });
            const auto found = completed_jobs_.find(sequence);
            if (found == completed_jobs_.end()) {
                throw std::runtime_error("detection executor stopped early");
            }
            job = std::move(found->second);
            completed_jobs_.erase(found);
        }

        const auto error = job->error;
        DetectionResult result = std::move(job->result);
        {
            std::lock_guard lock(mutex_);
            free_jobs_.push_back(std::move(job));
        }
        free_ready_.notify_one();
        if (error) {
            std::rethrow_exception(error);
        }
        return result;
    }

    [[nodiscard]] std::size_t queue_depth() const noexcept {
        return queue_depth_;
    }

private:
    struct Job {
        DetectionResult result;
        std::vector<uestcradar::ComplexFloat32> samples;
        std::exception_ptr error;
    };

    void worker_loop() noexcept {
        for (;;) {
            std::unique_ptr<Job> job;
            {
                std::unique_lock lock(mutex_);
                jobs_ready_.wait(lock, [this] {
                    return stopping_ || !pending_jobs_.empty();
                });
                if (stopping_ && pending_jobs_.empty()) {
                    return;
                }
                job = std::move(pending_jobs_.front());
                pending_jobs_.pop_front();
            }

            try {
                if (job->result.metadata.pulses_per_cpi ==
                    config_.pulses_per_cpi) {
                    if (job->result.rows == 0 || job->samples.empty()) {
                        throw std::runtime_error(
                            "PulseCompression frame has no data channel");
                    }
                    job->result.detection = detect_target(
                        job->samples, config_);
                }
            } catch (...) {
                job->error = std::current_exception();
            }

            const auto sequence = job->result.sequence;
            {
                std::lock_guard lock(mutex_);
                completed_jobs_.emplace(sequence, std::move(job));
            }
            results_ready_.notify_all();
        }
    }

    TargetConfig config_;
    std::size_t queue_depth_{};
    std::mutex mutex_;
    std::condition_variable jobs_ready_;
    std::condition_variable results_ready_;
    std::condition_variable free_ready_;
    bool stopping_{};
    std::deque<std::unique_ptr<Job>> free_jobs_;
    std::deque<std::unique_ptr<Job>> pending_jobs_;
    std::map<std::uint64_t, std::unique_ptr<Job>> completed_jobs_;
    std::vector<std::thread> workers_;
};

}  // namespace radar_sink
