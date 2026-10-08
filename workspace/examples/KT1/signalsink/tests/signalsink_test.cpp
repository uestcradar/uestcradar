#include "../src/recorder.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>

using namespace signalsink;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string field(const std::string& status, const std::string& name) {
    const auto key = "\"" + name + "\":\"";
    auto begin = status.find(key);
    if (begin == std::string::npos) throw std::runtime_error("missing field");
    begin += key.size();
    return status.substr(begin, status.find('"', begin) - begin);
}
void wait_state(Recorder& recorder, const std::string& expected) {
    for (int i = 0; i < 500; ++i) {
        if (field(recorder.status(), "state") == expected) return;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    throw std::runtime_error(recorder.status());
}
}
int main() {
    char temp[] = "/tmp/signalsink-test-XXXXXX";
    const auto* root = ::mkdtemp(temp);
    if (!root) return 1;
    try {
        require(!valid_directory("../escape") && !valid_directory("/root") && !valid_directory("a//b"), "path validation");
        require(valid_directory("runs/a-1"), "valid path rejected");
        require(json_string("\"\n\\") == "\"\\\"\\u000a\\\\\"", "JSON escaping");
        std::string id;
        std::array<std::byte, 77> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::byte(i);
        {
            Recorder recorder(root, 1024, 1024);
            recorder.accept(bytes);
            require(field(recorder.status(), "accepted_frames") == "0", "default must not record");
            recorder.start("runs/a-1");
            id = field(recorder.status(), "recording_id");
            bool rejected = false;
            try { recorder.start("second"); } catch (...) { rejected = true; }
            require(rejected, "duplicate start");
            recorder.accept(bytes);
            recorder.stop(id);
            recorder.stop(id);
            recorder.accept(bytes);
            wait_state(recorder, "idle");
            require(field(recorder.status(), "written_frames") == "1", "stop boundary");
            require(field(recorder.status(), "queue_used_bytes") == "0", "queue not drained");
            rejected = false;
            try { recorder.start("../invalid"); } catch (...) { rejected = true; }
            require(rejected && field(recorder.status(), "recording_id").empty() && field(recorder.status(), "written_frames") == "0", "failed start reused previous recording statistics");
            recorder.start("runs/a-1");
            const auto next = field(recorder.status(), "recording_id");
            rejected = false;
            try { recorder.stop(id); } catch (...) { rejected = true; }
            require(rejected, "stale stop ID");
            recorder.stop(next);
            wait_state(recorder, "idle"); // No input needed to finish.
        }
        {
            Recorder recorder(root, 1024, 1024);
            require(field(recorder.status(), "directory") == "runs/a-1", "directory not persistent");
            require(field(recorder.status(), "state") == "idle", "restart must not resume");
            recorder.start("overflow");
            std::array<std::byte, 1024> too_large{};
            recorder.accept(too_large);
            wait_state(recorder, "failed");
            recorder.accept(bytes);
            require(field(recorder.status(), "accepted_frames") == "0", "overflow silently resumed");
        }
        {
            Recorder recorder(root, 1024, 1024);
            recorder.start("wrap");
            const auto wrap_id = field(recorder.status(), "recording_id");
            for (unsigned i = 1; i <= 128; ++i) {
                recorder.accept(bytes);
                for (unsigned retry = 0; field(recorder.status(), "written_frames") != std::to_string(i); ++retry) {
                    require(retry < 500, "wrapped queue did not drain");
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
            }
            recorder.stop(wrap_id);
            wait_state(recorder, "idle");
            std::ifstream capture(std::string(root) + "/wrap/" + wrap_id + ".sink", std::ios::binary);
            std::vector<char> data((std::istreambuf_iterator<char>(capture)), {});
            require(data.size() == 8 + 128 * (8 + bytes.size()) + 24, "wrapped output size");
            for (unsigned i = 0; i < 128; ++i)
                require(std::memcmp(data.data() + 16 + i * (8 + bytes.size()), bytes.data(), bytes.size()) == 0, "wrapped frame corrupted");
        }
        {
            std::vector<std::byte> large(16 * 1024 * 1024, std::byte{42});
            Recorder recorder(root, large.size() + 1024, large.size() * 2);
            for (unsigned run = 0; run < 2; ++run) {
                recorder.start("large-frame");
                const auto recording = field(recorder.status(), "recording_id");
                recorder.accept(large);
                recorder.stop(recording);
                wait_state(recorder, "idle");
                std::ifstream file(std::string(root) + "/large-frame/" + recording + ".sink", std::ios::binary);
                file.seekg(16);
                std::vector<std::byte> stored(large.size());
                file.read(reinterpret_cast<char*>(stored.data()), stored.size());
                require(file.good() && stored == large, "large frame changed stored bytes");
                require(field(recorder.status(), "written_frames") == "1", "large frame count");
            }
        }
        {
            Recorder recorder(root, 1024, std::numeric_limits<std::uint64_t>::max());
            bool rejected = false;
            try { recorder.start("no-space"); } catch (...) { rejected = true; }
            require(rejected && field(recorder.status(), "state") == "failed", "free-space check");
        }
        require(::symlink("/tmp", (std::string(root) + "/escape").c_str()) == 0, "create symlink fixture");
        {
            Recorder recorder(root, 1024, 1024);
            bool rejected = false;
            try { recorder.start("escape"); } catch (...) { rejected = true; }
            require(rejected, "symlink escape");
        }
        std::ifstream capture(std::string(root) + "/runs/a-1/" + id + ".sink", std::ios::binary);
        std::vector<char> stored((std::istreambuf_iterator<char>(capture)), {});
        require(stored.size() == 8 + 8 + bytes.size() + 24, "wrong capture length");
        require(std::memcmp(stored.data(), "USINK001", 8) == 0, "capture magic");
        require(std::memcmp(stored.data() + 16, bytes.data(), bytes.size()) == 0, "raw bytes changed");
        require(load64(reinterpret_cast<const std::byte*>(stored.data() + stored.size() - 16)) == 1, "footer frames");
        require(load64(reinterpret_cast<const std::byte*>(stored.data() + stored.size() - 8)) == bytes.size(), "footer bytes");
        Fd full(::open("/dev/full", O_WRONLY));
        bool rejected = false;
        try { write_all(full.get(), bytes.data(), bytes.size()); } catch (...) { rejected = true; }
        require(rejected, "write failure not detected");
        const auto child = ::fork();
        require(child >= 0, "fork failure injection");
        if (child == 0) {
            try {
                std::signal(SIGXFSZ, SIG_IGN);
                const rlimit limit{100, 100};
                if (::setrlimit(RLIMIT_FSIZE, &limit)) ::_exit(2);
                {
                    Recorder recorder(root, 1024, 1024);
                    recorder.start("disk-error");
                    const auto recording = field(recorder.status(), "recording_id");
                    recorder.accept(bytes);
                    recorder.stop(recording);
                    wait_state(recorder, "failed");
                    require(!std::filesystem::exists(std::string(root) + "/disk-error/" + recording + ".sink"), "failed write published complete capture");
                }
                ::_exit(0);
            } catch (...) { ::_exit(1); }
        }
        int child_status = 0;
        require(::waitpid(child, &child_status, 0) == child && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0, "real short-write/finalization failure test");
        std::filesystem::remove_all(root);
        std::cout << "SignalSink recorder checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (fixtures preserved in " << root << ")\n";
        return 1;
    }
}
