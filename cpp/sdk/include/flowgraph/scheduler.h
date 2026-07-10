#ifndef CYCORE_SCHEDULER_H
#define CYCORE_SCHEDULER_H

#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

namespace cy::flowgraph {

class Graph;

enum class SchedulerState {
    Idle,
    Initialised,
    Running,
    Stopping,
    Stopped,
    Failed
};

struct SchedulerOptions {
    std::size_t yield_every_iterations = 64;
    std::size_t sleep_after_iterations = 4096;
    std::chrono::microseconds idle_sleep{50};
};

class SchedulerError : public std::runtime_error {
public:
    explicit SchedulerError(const std::string& message);
};

class ThreadPerBlockScheduler {
public:
    explicit ThreadPerBlockScheduler(Graph& graph,
                                     SchedulerOptions options = {});
    ~ThreadPerBlockScheduler();

    ThreadPerBlockScheduler(const ThreadPerBlockScheduler&) = delete;
    ThreadPerBlockScheduler& operator=(const ThreadPerBlockScheduler&) = delete;
    ThreadPerBlockScheduler(ThreadPerBlockScheduler&&) = delete;
    ThreadPerBlockScheduler& operator=(ThreadPerBlockScheduler&&) = delete;

    void init();
    void start();
    void request_stop() noexcept;
    void wait();
    void stop();

    SchedulerState state() const noexcept;
    bool failed() const noexcept;
    std::string error_message() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cy::flowgraph

#endif // CYCORE_SCHEDULER_H
