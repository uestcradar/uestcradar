#ifndef CYCORE_FLOWGRAPH_PROBE_H
#define CYCORE_FLOWGRAPH_PROBE_H

#include "data_type_traits.h"

#include <common/i_probe.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cy::flowgraph {

namespace detail {

constexpr std::size_t kDefaultProbeMaxElements = 4096;
constexpr std::size_t kDefaultProbeMaxBytes = 64 * 1024;

} // namespace detail

template <typename T>
class Probe final : public cy::common::IProbe {
public:
    Probe(std::string topic,
          std::size_t frame_size,
          std::size_t max_elements = detail::kDefaultProbeMaxElements,
          std::size_t max_bytes = detail::kDefaultProbeMaxBytes)
        : topic_(std::move(topic)),
          frame_size_(std::min(frame_size == 0 ? detail::kDefaultProbeMaxElements : frame_size,
                               max_elements)),
          max_bytes_(std::min(max_bytes, frame_size_ * sizeof(T))),
          snapshot_(max_bytes_) {
        if (topic_.empty()) {
            throw std::invalid_argument("probe topic must not be empty");
        }
        if (frame_size_ == 0 || max_bytes_ < sizeof(T)) {
            throw std::invalid_argument("probe frame size must be greater than zero");
        }
    }

    const std::string& topic() const noexcept override { return topic_; }
    cy::common::DataType data_type() const noexcept override { return DataTypeTraits<T>::value; }
    std::size_t element_size() const noexcept override { return sizeof(T); }
    std::size_t frame_size() const noexcept override { return frame_size_; }

    void request() noexcept override {
        request_pending_.store(true, std::memory_order_release);
    }

    std::size_t peek_latest(cy::common::Span<std::byte> buffer) const override {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        const std::size_t bytes = std::min(buffer.size(), snapshot_size_);
        if (bytes == 0) {
            return 0;
        }
        std::memcpy(buffer.data(), snapshot_.data(), bytes);
        return bytes;
    }

    void capture_latest(const T* data, std::size_t count) noexcept {
        if (!request_pending_.load(std::memory_order_acquire) || data == nullptr || count == 0) {
            return;
        }
        if (!snapshot_mutex_.try_lock()) {
            return;
        }

        std::unique_lock<std::mutex> lock(snapshot_mutex_, std::adopt_lock);
        try {
            const std::size_t max_elements_by_bytes = max_bytes_ / sizeof(T);
            const std::size_t elements = std::min(count, std::min(frame_size_, max_elements_by_bytes));
            const std::size_t offset = count - elements;
            const std::size_t bytes = elements * sizeof(T);
            std::memcpy(snapshot_.data(), data + offset, bytes);
            snapshot_size_ = bytes;
            request_pending_.store(false, std::memory_order_release);
        } catch (...) {
            // Probe capture is a passive observation path; never disturb the writer.
        }
    }

    static void capture_latest_hook(void* probe, const T* data, std::size_t count) noexcept {
        if (!probe) {
            return;
        }
        static_cast<Probe<T>*>(probe)->capture_latest(data, count);
    }

private:
    std::string topic_;
    std::size_t frame_size_ = 0;
    std::size_t max_bytes_ = 0;

    std::atomic<bool> request_pending_{false};
    mutable std::mutex snapshot_mutex_;
    std::vector<std::byte> snapshot_;
    std::size_t snapshot_size_ = 0;
};

class ProbeRegistry final : public cy::common::IProbeProvider {
public:
    void AddProbe(std::shared_ptr<cy::common::IProbe> probe) {
        if (!probe) {
            throw std::invalid_argument("probe registry cannot add null probe");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string topic = probe->topic();
        if (!probes_.emplace(topic, std::move(probe)).second) {
            throw std::invalid_argument("duplicate probe topic: " + topic);
        }
    }

    std::shared_ptr<cy::common::IProbe> GetProbe(const std::string& topic) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = probes_.find(topic);
        if (it == probes_.end()) {
            return {};
        }
        return it->second;
    }

    std::vector<std::string> ListProbeTopics() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> topics;
        topics.reserve(probes_.size());
        for (const auto& item : probes_) {
            topics.push_back(item.first);
        }
        return topics;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<cy::common::IProbe>> probes_;
};

} // namespace cy::flowgraph

#endif // CYCORE_FLOWGRAPH_PROBE_H
