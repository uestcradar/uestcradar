#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace signalsink {

inline void io_error(const char* action) {
    throw std::runtime_error(std::string(action) + ": " + std::strerror(errno));
}

class Fd {
public:
    explicit Fd(int value = -1) : value_(value) {}
    ~Fd() { if (value_ >= 0) ::close(value_); }
    Fd(Fd&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) { if (value_ >= 0) ::close(value_); value_ = std::exchange(other.value_, -1); }
        return *this;
    }
    int get() const { return value_; }
    void close_checked() {
        const int fd = std::exchange(value_, -1);
        if (fd >= 0 && ::close(fd) != 0) io_error("close"); // Linux: never retry close after EINTR.
    }
private:
    int value_;
};

inline std::string json_string(const std::string& text) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char ch : text) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch < 32) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += static_cast<char>(ch);
    }
    return out + '"';
}

inline void write_all(int fd, const void* data, std::size_t size) {
    auto bytes = static_cast<const std::byte*>(data);
    while (size) {
        const auto count = ::write(fd, bytes, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { if (!count) errno = EIO; io_error("write"); }
        bytes += count;
        size -= static_cast<std::size_t>(count);
    }
}

inline void sync_fd(int fd) {
    while (::fsync(fd) != 0) { if (errno != EINTR) io_error("fsync"); }
}

inline std::array<std::byte, 8> little64(std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (unsigned i = 0; i < 8; ++i) bytes[i] = std::byte((value >> (i * 8)) & 255);
    return bytes;
}
inline std::uint64_t load64(const std::byte* bytes) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(std::to_integer<unsigned>(bytes[i])) << (i * 8);
    return value;
}
inline std::string new_id() {
    std::array<unsigned char, 16> bytes{};
    std::size_t done = 0;
    while (done < bytes.size()) {
        auto count = ::getrandom(bytes.data() + done, bytes.size() - done, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) io_error("getrandom");
        done += static_cast<std::size_t>(count);
    }
    std::string id;
    for (auto byte : bytes) { id += "0123456789abcdef"[byte >> 4]; id += "0123456789abcdef"[byte & 15]; }
    return id;
}
inline bool valid_directory(const std::string& path) {
    if (path.empty() || path.size() > 240 || path.front() == '/' || path.back() == '/') return false;
    std::size_t begin = 0;
    while (begin < path.size()) {
        auto end = path.find('/', begin);
        if (end == std::string::npos) end = path.size();
        const auto part = path.substr(begin, end - begin);
        if (part.empty() || part == "." || part == ".." || part.front() == '.') return false;
        for (unsigned char ch : part) {
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return false;
        }
        begin = end + 1;
    }
    return true;
}
inline Fd open_directory(int root, const std::string& path, bool create) {
    if (!valid_directory(path)) throw std::invalid_argument("invalid relative capture directory");
    Fd current(::dup(root));
    if (current.get() < 0) io_error("dup capture root");
    std::size_t begin = 0;
    while (begin < path.size()) {
        auto end = path.find('/', begin);
        if (end == std::string::npos) end = path.size();
        const auto part = path.substr(begin, end - begin);
        if (create && ::mkdirat(current.get(), part.c_str(), 0700) && errno != EEXIST) io_error("mkdir capture directory");
        Fd next(::openat(current.get(), part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (next.get() < 0) io_error("open capture directory");
        if (create) sync_fd(current.get());
        current = std::move(next);
        begin = end + 1;
    }
    return current;
}

// ponytail: one sequential writer; add parallel disk writers only after measuring a bottleneck.
class Recorder {
public:
    Recorder(const std::string& root, std::size_t capacity, std::uint64_t min_free)
        : root_(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)),
          capacity_(capacity), min_free_(min_free) {
        if (root_.get() < 0) io_error("open capture root (must already exist)");
        if (capacity < 1024 || capacity > (std::uint64_t{8} << 30) || min_free < capacity)
            throw std::invalid_argument("queue must be 1 KiB..8 GiB and free-space reserve >= queue");
        Fd config(::openat(root_.get(), ".signalsink-directory", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
        if (config.get() >= 0) {
            struct stat info{};
            if (::fstat(config.get(), &info) || !S_ISREG(info.st_mode) || info.st_size > 240)
                throw std::runtime_error("invalid saved capture directory file");
            char saved[241]{};
            auto count = ::read(config.get(), saved, 240);
            if (count <= 0 || !valid_directory(std::string(saved, static_cast<std::size_t>(count))))
                throw std::runtime_error("invalid saved capture directory");
            directory_.assign(saved, static_cast<std::size_t>(count));
        } else if (errno != ENOENT) io_error("read capture configuration");
        writer_ = std::thread([this] { write_loop(); });
    }
    ~Recorder() {
        {
            std::lock_guard lock(mutex_);
            if (state_ == "recording") state_ = "stopping";
            exiting_ = true;
            wake_.notify_all();
        }
        writer_.join();
    }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Control commands are serialized by the single local control listener.
    void start(const std::string& directory) {
        {
            std::lock_guard lock(mutex_);
            if (state_ != "idle" && state_ != "failed") throw std::runtime_error("recording is busy");
            if (file_.get() >= 0) throw std::runtime_error("failed recording is still closing");
            state_ = "starting";
            error_.clear();
            id_.clear();
            accepted_ = written_ = bytes_ = 0;
            elapsed_ms_ = 0;
            started_ = std::chrono::steady_clock::now();
        }
        try {
            auto dir = open_directory(root_.get(), directory, true);
            check_space(dir.get());
            std::vector<std::byte> buffer(capacity_);
            const auto id = new_id();
            Fd file(::openat(dir.get(), (id + ".partial").c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
            if (file.get() < 0) io_error("create capture");
            write_all(file.get(), "USINK001", 8);
            const auto temporary = ".directory-" + id;
            Fd setting(::openat(root_.get(), temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
            if (setting.get() < 0) io_error("create directory setting");
            try {
                write_all(setting.get(), directory.data(), directory.size());
                sync_fd(setting.get());
                if (::renameat(root_.get(), temporary.c_str(), root_.get(), ".signalsink-directory")) io_error("save directory setting");
                sync_fd(root_.get());
            } catch (...) { ::unlinkat(root_.get(), temporary.c_str(), 0); throw; }
            std::lock_guard lock(mutex_);
            buffer_ = std::move(buffer);
            file_ = std::move(file);
            dir_ = std::move(dir);
            id_ = id;
            directory_ = directory;
            head_ = used_ = 0;
            accepted_ = written_ = bytes_ = 0;
            started_ = std::chrono::steady_clock::now();
            elapsed_ms_ = 0;
            state_ = "recording";
        } catch (const std::exception& error) {
            std::lock_guard lock(mutex_);
            error_ = error.what(); state_ = "failed";
            wake_.notify_all();
            throw;
        }
    }
    void stop(const std::string& id) {
        std::lock_guard lock(mutex_);
        if (id.empty() || id != id_) throw std::runtime_error("recording ID does not match");
        if (state_ == "recording") { state_ = "stopping"; wake_.notify_all(); }
        else if (state_ != "stopping" && state_ != "idle") throw std::runtime_error("recording is not active");
    }
    void fail(const std::string& error) {
        std::lock_guard lock(mutex_);
        if (state_ == "recording") {
            state_ = "failed"; error_ = error; wake_.notify_all();
        }
    }
    void accept(std::span<const std::byte> frame) {
        std::lock_guard lock(mutex_);
        if (state_ != "recording") return;
        if (frame.size() < 64 || frame.size() > capacity_ - 8 || frame.size() + 8 > capacity_ - used_) {
            state_ = "failed"; error_ = "recording queue capacity exceeded"; wake_.notify_all(); return;
        }
        auto tail = (head_ + used_) % capacity_;
        auto copy = [&](const std::byte* bytes, std::size_t count) {
            const auto first = std::min(count, capacity_ - tail);
            std::memcpy(buffer_.data() + tail, bytes, first);
            std::memcpy(buffer_.data(), bytes + first, count - first);
            tail = (tail + count) % capacity_;
        };
        const auto length = little64(frame.size());
        copy(length.data(), length.size()); copy(frame.data(), frame.size());
        used_ += 8 + frame.size(); ++accepted_;
        wake_.notify_all();
    }
    std::string status() {
        std::string result, directory;
        Fd target;
        bool active_target = false;
        {
            std::lock_guard lock(mutex_);
            active_target = dir_.get() >= 0;
            if (active_target) target = Fd(::dup(dir_.get()));
            const auto elapsed = (state_ == "recording" || state_ == "stopping")
                ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count() : elapsed_ms_;
            directory = directory_;
            result = "{\"ok\":true,\"process_id\":\"" + std::to_string(::getpid()) + "\",\"state\":" + json_string(state_) + ",\"recording_id\":" + json_string(id_) +
                ",\"directory\":" + json_string(directory_) + ",\"error\":" + json_string(error_) +
                ",\"accepted_frames\":\"" + std::to_string(accepted_) + "\",\"written_frames\":\"" + std::to_string(written_) +
                "\",\"written_bytes\":\"" + std::to_string(bytes_) + "\",\"queue_used_bytes\":\"" + std::to_string(used_) +
                "\",\"queue_capacity_bytes\":\"" + std::to_string(capacity_) + "\",\"elapsed_ms\":\"" + std::to_string(elapsed) +
                "\",\"sample_continuity\":\"unverified\"";
        }
        try {
            if (active_target && target.get() < 0) throw std::runtime_error("cannot inspect active capture volume");
            if (!active_target) target = open_directory(root_.get(), directory, false);
            struct statvfs disk{};
            if (::fstatvfs(target.get(), &disk)) io_error("statvfs");
            const std::uint64_t unit = disk.f_frsize;
            result += ",\"disk_total_bytes\":\"" + std::to_string(unit * disk.f_blocks) +
                "\",\"disk_used_bytes\":\"" + std::to_string(unit * (disk.f_blocks - disk.f_bfree)) +
                "\",\"disk_available_bytes\":\"" + std::to_string(unit * disk.f_bavail) + "\"";
        } catch (const std::exception& error) {
            result += ",\"disk_total_bytes\":null,\"disk_used_bytes\":null,\"disk_available_bytes\":null,\"disk_error\":" + json_string(error.what());
        }
        return result + "}";
    }
private:
    void check_space(int fd) const {
        struct statvfs disk{};
        if (::fstatvfs(fd, &disk)) io_error("statvfs");
        if (std::uint64_t(disk.f_frsize) * disk.f_bavail <= min_free_)
            throw std::runtime_error("capture disk free-space reserve reached");
    }
    void write_loop() {
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [&] { return used_ || state_ == "stopping" || (state_ == "failed" && file_.get() >= 0) || exiting_; });
            if (state_ == "failed") {
                file_ = Fd{}; dir_ = Fd{}; used_ = 0;
                std::vector<std::byte>{}.swap(buffer_);
                elapsed_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count();
            }
            if (exiting_ && file_.get() < 0) return;
            if (file_.get() < 0) continue;
            if (used_) {
                std::array<std::byte, 8> length{};
                for (std::size_t i = 0; i < 8; ++i) length[i] = buffer_[(head_ + i) % capacity_];
                const auto payload = static_cast<std::size_t>(load64(length.data()));
                const auto size = payload + 8;
                const auto first = std::min(size, capacity_ - head_);
                const auto* begin = buffer_.data() + head_;
                lock.unlock();
                std::string error;
                try {
                    check_space(dir_.get());
                    write_all(file_.get(), begin, first);
                    write_all(file_.get(), buffer_.data(), size - first);
                } catch (const std::exception& value) { error = value.what(); }
                lock.lock();
                if (!error.empty()) { state_ = "failed"; error_ = error; }
                else { head_ = (head_ + size) % capacity_; used_ -= size; ++written_; bytes_ += payload; }
                continue;
            }
            if (state_ == "stopping") {
                // Keep stopping state visible while fsync runs; input consumption continues.
                lock.unlock();
                std::string error;
                try {
                    const auto zero = little64(0), frames = little64(written_), bytes = little64(bytes_);
                    write_all(file_.get(), zero.data(), 8); write_all(file_.get(), frames.data(), 8); write_all(file_.get(), bytes.data(), 8);
                    sync_fd(file_.get());
                    file_.close_checked();
                    const auto partial = id_ + ".partial", complete = id_ + ".sink";
                    if (::linkat(dir_.get(), partial.c_str(), dir_.get(), complete.c_str(), 0)) io_error("publish completed capture");
                    try { sync_fd(dir_.get()); }
                    catch (...) { ::unlinkat(dir_.get(), complete.c_str(), 0); throw; }
                    // The complete file is now durable. A crash may leave an extra partial hardlink.
                    if (::unlinkat(dir_.get(), partial.c_str(), 0)) io_error("remove partial link");
                    sync_fd(dir_.get());
                } catch (const std::exception& value) { error = value.what(); }
                lock.lock();
                file_ = Fd{}; dir_ = Fd{}; std::vector<std::byte>{}.swap(buffer_);
                elapsed_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count();
                if (error.empty() && state_ == "stopping") state_ = "idle";
                else { state_ = "failed"; if (!error.empty()) error_ = error; }
            }
        }
    }
    Fd root_, dir_, file_;
    const std::size_t capacity_;
    const std::uint64_t min_free_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread writer_;
    std::vector<std::byte> buffer_;
    std::size_t head_{}, used_{};
    std::uint64_t accepted_{}, written_{}, bytes_{};
    std::string state_{"idle"}, directory_{"recordings"}, id_, error_;
    std::chrono::steady_clock::time_point started_{};
    std::int64_t elapsed_ms_{};
    bool exiting_{};
};

} // namespace signalsink
