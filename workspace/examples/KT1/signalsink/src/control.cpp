#include "recorder.hpp"

#include <atomic>
#include <iostream>
#include <sstream>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace signalsink {
namespace {
constexpr char control_dir[] = "/tmp/uestcradar-signalsink";
constexpr char socket_path[] = "/tmp/uestcradar-signalsink/control.sock";

sockaddr_un address() {
    sockaddr_un value{};
    value.sun_family = AF_UNIX;
    std::strcpy(value.sun_path, socket_path);
    return value;
}
void deadlines(int socket) {
    const timeval limit{2, 0};
    if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit)) ||
        ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit))) io_error("socket timeout");
}
std::string receive(int socket, std::size_t limit) {
    std::string result;
    char buffer[1024];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("control receive timed out");
        const auto count = ::recv(socket, buffer, sizeof(buffer), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) io_error("receive control");
        if (!count) return result;
        if (result.size() + static_cast<std::size_t>(count) > limit) throw std::runtime_error("control message too large");
        result.append(buffer, static_cast<std::size_t>(count));
    }
}
void send_text(int socket, const std::string& text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        auto count = ::send(socket, text.data() + offset, text.size() - offset, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) io_error("send control");
        offset += static_cast<std::size_t>(count);
    }
}
}

int control_client(int argc, char** argv) {
    std::string request;
    if (argc == 3 && std::string(argv[2]) == "status") request = "status";
    else if (argc == 5 && std::string(argv[2]) == "start" && std::string(argv[3]) == "--directory" && valid_directory(argv[4]))
        request = "start " + std::string(argv[4]);
    else if (argc == 5 && std::string(argv[2]) == "stop" && std::string(argv[3]) == "--recording-id") {
        std::string id = argv[4];
        if (id.size() != 32 || id.find_first_not_of("0123456789abcdef") != std::string::npos) throw std::invalid_argument("invalid recording ID");
        request = "stop " + id;
    } else throw std::invalid_argument("control status | start --directory RELATIVE_PATH | stop --recording-id ID");
    Fd socket(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (socket.get() < 0) io_error("control socket");
    deadlines(socket.get());
    const auto endpoint = address();
    if (::connect(socket.get(), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint))) io_error("connect control");
    send_text(socket.get(), request);
    ::shutdown(socket.get(), SHUT_WR);
    std::cout << receive(socket.get(), 65536) << '\n';
    return 0;
}

// The listening fd and lock live in the main thread until the server has joined.
Fd control_listener(Fd& lock) {
    if (::mkdir(control_dir, 0700) && errno != EEXIST) io_error("create private control directory");
    struct stat info{};
    if (::lstat(control_dir, &info) || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid() || (info.st_mode & 077))
        throw std::runtime_error("control directory must be private and owned by Worker user");
    lock = Fd(::open((std::string(control_dir) + "/lock").c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (lock.get() < 0 || ::flock(lock.get(), LOCK_EX | LOCK_NB)) throw std::runtime_error("another SignalSink owns the control endpoint");
    if (::lstat(socket_path, &info) == 0) {
        if (!S_ISSOCK(info.st_mode) || info.st_uid != ::geteuid()) throw std::runtime_error("unsafe control socket path");
        if (::unlink(socket_path)) io_error("remove stale socket");
    } else if (errno != ENOENT) io_error("inspect control socket");
    Fd listener(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (listener.get() < 0) io_error("listen socket");
    const auto endpoint = address();
    if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) || ::listen(listener.get(), 4)) io_error("listen control");
    return listener;
}

void control_server(int listener, Recorder& recorder, const std::atomic<bool>& stopping) {
    while (!stopping.load()) {
        pollfd item{listener, POLLIN, 0};
        const auto ready = ::poll(&item, 1, 100);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return;
        if (!ready) continue;
        Fd client(::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC));
        if (client.get() < 0) continue;
        try {
            deadlines(client.get());
            const auto request = receive(client.get(), 512);
            std::istringstream fields(request);
            std::string action, argument, excess;
            fields >> action;
            if (action == "status") {
                if (fields >> excess) throw std::invalid_argument("unexpected status argument");
            } else if (action == "start" || action == "stop") {
                if (!(fields >> argument) || (fields >> excess)) throw std::invalid_argument("expected one control argument");
                if (action == "start") recorder.start(argument);
                else recorder.stop(argument);
            } else throw std::invalid_argument("unknown control operation");
            send_text(client.get(), recorder.status());
        } catch (const std::exception& error) {
            try { send_text(client.get(), "{\"ok\":false,\"error\":" + json_string(error.what()) + "}"); }
            catch (...) { /* A disconnected client must not change recording state. */ }
        }
    }
    ::unlink(socket_path);
}
} // namespace signalsink
