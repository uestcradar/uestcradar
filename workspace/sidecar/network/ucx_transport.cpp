#include "ucx_transport.hpp"

#include <ucp/api/ucp.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <netdb.h>
#include <sys/socket.h>

namespace sidecar::network {
namespace {

constexpr std::uint64_t kHandshakeTag = UINT64_MAX;
constexpr std::uint64_t kHandshakeAckTag = UINT64_MAX - 1;
constexpr std::byte kHandshakeByte{0x5a};

[[noreturn]] void throw_ucx(const char* operation, ucs_status_t status) {
    throw std::runtime_error(
        std::string{operation} + ": " + ucs_status_string(status));
}

[[noreturn]] void throw_timeout(const char* operation) {
    throw std::runtime_error(std::string{operation} + ": timed out");
}

class Address {
public:
    Address(
        const std::string& host,
        std::uint16_t port,
        bool passive) {
        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = passive ? AI_PASSIVE : 0;

        const std::string service = std::to_string(port);
        const char* node = host.empty() ? nullptr : host.c_str();
        const int result =
            ::getaddrinfo(node, service.c_str(), &hints, &addresses_);
        if (result != 0) {
            throw std::runtime_error(
                std::string{"getaddrinfo: "} + ::gai_strerror(result));
        }
    }

    Address(const Address&) = delete;
    Address& operator=(const Address&) = delete;

    ~Address() {
        if (addresses_ != nullptr) {
            ::freeaddrinfo(addresses_);
        }
    }

    [[nodiscard]] const struct addrinfo* get() const noexcept {
        return addresses_;
    }

private:
    struct addrinfo* addresses_{nullptr};
};

}  // namespace

struct UCXRequest::State {
    void* native_request{nullptr};
    ucs_status_t status{UCS_INPROGRESS};
    std::size_t bytes{0};
    bool complete{false};
    bool receive{false};
    std::weak_ptr<UCXTransport::Impl> owner;
    std::shared_ptr<UCXMemoryRegion::State> memory;
};

struct UCXMemoryRegion::State {
    std::shared_ptr<UCXTransport::Impl> owner;
    ucp_mem_h handle{nullptr};
    std::byte* address{nullptr};
    std::size_t length{0};

    ~State();
};

struct UCXTransport::Impl : std::enable_shared_from_this<Impl> {
    ucp_context_h context{nullptr};
    ucp_worker_h worker{nullptr};
    ucp_ep_h endpoint{nullptr};
    ucs_status_t endpoint_status{UCS_OK};
    std::unordered_set<UCXRequest::State*> requests;

    ~Impl() {
        close();
    }

    void initialize(DataPathMode data_path) {
        ucp_config_t* config = nullptr;
        ucs_status_t status = ::ucp_config_read(nullptr, nullptr, &config);
        if (status != UCS_OK) {
            throw_ucx("ucp_config_read", status);
        }

        const auto configure = [config](
                                   const char* name,
                                   const char* value) {
            const ucs_status_t result =
                ::ucp_config_modify(config, name, value);
            if (result != UCS_OK) {
                throw_ucx(name, result);
            }
        };

        try {
            configure("TCP_CM_REUSEADDR", "y");
            configure("RDMA_CM_REUSEADDR", "y");
            if (data_path == DataPathMode::strict_rdma) {
                const char* configured_tls = std::getenv("UCX_TLS");
                configure(
                    "TLS",
                    (configured_tls != nullptr && configured_tls[0] != '\0')
                        ? configured_tls
                        : "rc_verbs");
                configure("RNDV_THRESH", "0");
                configure("ZCOPY_THRESH", "0");
                configure("RNDV_SCHEME", "get_zcopy");
            }
        } catch (...) {
            ::ucp_config_release(config);
            throw;
        }

        ucp_params_t context_params {};
        context_params.field_mask = UCP_PARAM_FIELD_FEATURES;
        context_params.features = UCP_FEATURE_TAG;
        status = ::ucp_init(&context_params, config, &context);
        ::ucp_config_release(config);
        if (status != UCS_OK) {
            throw_ucx("ucp_init", status);
        }

        ucp_worker_params_t worker_params {};
        worker_params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
        worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;
        status = ::ucp_worker_create(context, &worker_params, &worker);
        if (status != UCS_OK) {
            ::ucp_cleanup(context);
            context = nullptr;
            throw_ucx("ucp_worker_create", status);
        }
    }

    void register_request(UCXRequest::State* state) {
        requests.insert(state);
    }

    void release_request(UCXRequest::State& state, bool cancel) noexcept {
        if (state.native_request == nullptr) {
            requests.erase(&state);
            return;
        }

        if (cancel && !state.complete && worker != nullptr) {
            ::ucp_request_cancel(worker, state.native_request);
            for (int attempt = 0; attempt < 10'000 && !state.complete; ++attempt) {
                ::ucp_worker_progress(worker);
            }
        }
        ::ucp_request_free(state.native_request);
        state.native_request = nullptr;
        requests.erase(&state);
    }

    void check_endpoint() const {
        if (endpoint_status != UCS_OK) {
            throw_ucx("UCX endpoint", endpoint_status);
        }
    }

    void close() noexcept {
        if (worker != nullptr) {
            while (!requests.empty()) {
                UCXRequest::State* state = *requests.begin();
                release_request(*state, true);
                state->complete = true;
                if (state->status == UCS_INPROGRESS) {
                    state->status = UCS_ERR_CANCELED;
                }
            }
        }

        if (endpoint != nullptr && worker != nullptr) {
            ucp_request_param_t params {};
            params.op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS;
            params.flags = UCP_EP_CLOSE_FLAG_FORCE;
            void* request = ::ucp_ep_close_nbx(endpoint, &params);
            if (request != nullptr && !UCS_PTR_IS_ERR(request)) {
                while (::ucp_request_check_status(request) == UCS_INPROGRESS) {
                    ::ucp_worker_progress(worker);
                }
                ::ucp_request_free(request);
            }
            endpoint = nullptr;
        }
        if (worker != nullptr) {
            ::ucp_worker_destroy(worker);
            worker = nullptr;
        }
        if (context != nullptr) {
            ::ucp_cleanup(context);
            context = nullptr;
        }
    }
};

namespace {

bool contains(
    const UCXMemoryRegion::State& region,
    const std::byte* address,
    std::size_t length) noexcept {
    if (address == nullptr || region.address == nullptr ||
        length > region.length) {
        return false;
    }
    const auto begin =
        reinterpret_cast<std::uintptr_t>(region.address);
    const auto current =
        reinterpret_cast<std::uintptr_t>(address);
    if (current < begin) {
        return false;
    }
    const std::uintptr_t offset = current - begin;
    return offset <= region.length - length;
}

std::shared_ptr<UCXMemoryRegion::State> checked_memory(
    const std::shared_ptr<UCXTransport::Impl>& owner,
    std::shared_ptr<UCXMemoryRegion::State> memory,
    const std::byte* address,
    std::size_t length) {
    if (memory == nullptr) {
        return {};
    }
    if (memory->owner.get() != owner.get()) {
        throw std::invalid_argument(
            "UCX memory region belongs to another transport");
    }
    if (!contains(*memory, address, length)) {
        throw std::invalid_argument(
            "UCX buffer is outside the registered memory region");
    }
    return memory;
}

void endpoint_error(void* argument, ucp_ep_h, ucs_status_t status) {
    auto* impl = static_cast<UCXTransport::Impl*>(argument);
    impl->endpoint_status = status;
}

void send_complete(void*, ucs_status_t status, void* user_data) {
    auto* state = static_cast<UCXRequest::State*>(user_data);
    state->status = status;
    state->complete = true;
}

void receive_complete(
    void*,
    ucs_status_t status,
    const ucp_tag_recv_info_t* info,
    void* user_data) {
    auto* state = static_cast<UCXRequest::State*>(user_data);
    state->status = status;
    state->bytes = info == nullptr ? 0 : info->length;
    state->complete = true;
}

struct ConnectionSlot {
    ucp_conn_request_h request{nullptr};
};

void connection_received(ucp_conn_request_h request, void* argument) {
    auto* slot = static_cast<ConnectionSlot*>(argument);
    if (slot->request == nullptr) {
        slot->request = request;
    }
}

ucp_ep_params_t endpoint_parameters(UCXTransport::Impl* impl) {
    ucp_ep_params_t params {};
    params.field_mask =
        UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE |
        UCP_EP_PARAM_FIELD_ERR_HANDLER;
    params.err_mode = UCP_ERR_HANDLING_MODE_PEER;
    params.err_handler.cb = endpoint_error;
    params.err_handler.arg = impl;
    return params;
}

template <typename Predicate>
void progress_until(
    UCXTransport::Impl& impl,
    Predicate&& predicate,
    std::chrono::milliseconds timeout,
    const char* operation) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        impl.check_endpoint();
        if (::ucp_worker_progress(impl.worker) == 0) {
            std::this_thread::yield();
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw_timeout(operation);
        }
    }
}

void handshake_server(UCXTransport& transport, std::chrono::milliseconds timeout) {
    std::byte received{};
    UCXRequest request = transport.receive(
        std::span<std::byte>{&received, 1},
        kHandshakeTag);
    transport.wait(request, timeout);
    if (received != kHandshakeByte) {
        throw std::runtime_error("UCX handshake: invalid client byte");
    }
    UCXRequest ack = transport.send(
        std::span<const std::byte>{&kHandshakeByte, 1},
        kHandshakeAckTag);
    transport.wait(ack, timeout);
}

void handshake_client(UCXTransport& transport, std::chrono::milliseconds timeout) {
    UCXRequest request = transport.send(
        std::span<const std::byte>{&kHandshakeByte, 1},
        kHandshakeTag);
    transport.wait(request, timeout);
    std::byte received{};
    UCXRequest ack = transport.receive(
        std::span<std::byte>{&received, 1},
        kHandshakeAckTag);
    transport.wait(ack, timeout);
    if (received != kHandshakeByte) {
        throw std::runtime_error("UCX handshake: invalid server byte");
    }
}

}  // namespace

UCXMemoryRegion::State::~State() {
    if (handle != nullptr && owner != nullptr &&
        owner->context != nullptr) {
        ::ucp_mem_unmap(owner->context, handle);
    }
}

UCXRequest::UCXRequest() noexcept = default;

UCXRequest::UCXRequest(std::shared_ptr<State> state) noexcept
    : state_(std::move(state)) {}

UCXRequest::UCXRequest(UCXRequest&& other) noexcept = default;

UCXRequest& UCXRequest::operator=(UCXRequest&& other) noexcept {
    if (this != &other) {
        if (state_ != nullptr) {
            if (auto owner = state_->owner.lock()) {
                owner->release_request(*state_, true);
            }
        }
        state_ = std::move(other.state_);
    }
    return *this;
}

UCXRequest::~UCXRequest() {
    if (state_ != nullptr) {
        if (auto owner = state_->owner.lock()) {
            owner->release_request(*state_, true);
        }
    }
}

bool UCXRequest::completed() const noexcept {
    return state_ != nullptr && state_->complete;
}

std::size_t UCXRequest::bytes_transferred() const {
    if (state_ == nullptr || !state_->complete) {
        throw std::logic_error("UCX request has not completed");
    }
    if (state_->status != UCS_OK) {
        throw_ucx("UCX request", state_->status);
    }
    return state_->bytes;
}

UCXMemoryRegion::UCXMemoryRegion() noexcept = default;
UCXMemoryRegion::UCXMemoryRegion(
    std::shared_ptr<State> state) noexcept
    : state_(std::move(state)) {}
UCXMemoryRegion::UCXMemoryRegion(UCXMemoryRegion&& other) noexcept = default;
UCXMemoryRegion& UCXMemoryRegion::operator=(
    UCXMemoryRegion&& other) noexcept = default;
UCXMemoryRegion::~UCXMemoryRegion() = default;

bool UCXMemoryRegion::valid() const noexcept {
    return state_ != nullptr && state_->handle != nullptr;
}

std::size_t UCXMemoryRegion::size() const noexcept {
    return state_ == nullptr ? 0 : state_->length;
}

UCXTransport::UCXTransport(std::shared_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

UCXTransport::UCXTransport(UCXTransport&& other) noexcept = default;
UCXTransport& UCXTransport::operator=(UCXTransport&& other) noexcept = default;
UCXTransport::~UCXTransport() = default;

UCXTransport UCXTransport::accept_one(const EndpointOptions& options) {
    auto impl = std::make_shared<Impl>();
    impl->initialize(options.data_path);
    Address address{options.address, options.port, true};
    const struct addrinfo* resolved = address.get();

    ConnectionSlot slot;
    ucp_listener_params_t listener_params {};
    listener_params.field_mask =
        UCP_LISTENER_PARAM_FIELD_SOCK_ADDR |
        UCP_LISTENER_PARAM_FIELD_CONN_HANDLER;
    listener_params.sockaddr.addr = resolved->ai_addr;
    listener_params.sockaddr.addrlen = resolved->ai_addrlen;
    listener_params.conn_handler.cb = connection_received;
    listener_params.conn_handler.arg = &slot;

    ucp_listener_h listener = nullptr;
    const ucs_status_t listener_status =
        ::ucp_listener_create(impl->worker, &listener_params, &listener);
    if (listener_status != UCS_OK) {
        throw_ucx("ucp_listener_create", listener_status);
    }

    try {
        progress_until(
            *impl,
            [&slot] { return slot.request != nullptr; },
            options.timeout,
            "accept UCX connection");

        ucp_ep_params_t ep_params = endpoint_parameters(impl.get());
        ep_params.field_mask |= UCP_EP_PARAM_FIELD_CONN_REQUEST;
        ep_params.conn_request = slot.request;
        const ucs_status_t ep_status =
            ::ucp_ep_create(impl->worker, &ep_params, &impl->endpoint);
        if (ep_status != UCS_OK) {
            throw_ucx("ucp_ep_create(server)", ep_status);
        }
        ::ucp_listener_destroy(listener);
        listener = nullptr;

        UCXTransport transport{std::move(impl)};
        handshake_server(transport, options.timeout);
        return transport;
    } catch (...) {
        if (listener != nullptr) {
            ::ucp_listener_destroy(listener);
        }
        throw;
    }
}

UCXTransport UCXTransport::connect(const EndpointOptions& options) {
    auto impl = std::make_shared<Impl>();
    impl->initialize(options.data_path);
    Address address{options.address, options.port, false};
    const struct addrinfo* resolved = address.get();

    ucp_ep_params_t ep_params = endpoint_parameters(impl.get());
    ep_params.field_mask |=
        UCP_EP_PARAM_FIELD_FLAGS |
        UCP_EP_PARAM_FIELD_SOCK_ADDR;
    ep_params.flags = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER;
    ep_params.sockaddr.addr = resolved->ai_addr;
    ep_params.sockaddr.addrlen = resolved->ai_addrlen;
    const ucs_status_t status =
        ::ucp_ep_create(impl->worker, &ep_params, &impl->endpoint);
    if (status != UCS_OK) {
        throw_ucx("ucp_ep_create(client)", status);
    }

    UCXTransport transport{std::move(impl)};
    handshake_client(transport, options.timeout);
    return transport;
}

UCXRequest UCXTransport::send(
    std::span<const std::byte> buffer,
    std::uint64_t tag,
    const UCXMemoryRegion* memory) {
    if (impl_ == nullptr || impl_->endpoint == nullptr) {
        throw std::logic_error("UCX transport is not connected");
    }
    if (buffer.empty()) {
        throw std::invalid_argument("UCX send buffer must not be empty");
    }
    impl_->check_endpoint();

    auto state = std::make_shared<UCXRequest::State>();
    state->owner = impl_;
    state->bytes = buffer.size();
    state->memory = checked_memory(
        impl_,
        memory == nullptr ? nullptr : memory->state_,
        buffer.data(),
        buffer.size());

    ucp_request_param_t params {};
    params.op_attr_mask =
        UCP_OP_ATTR_FIELD_CALLBACK |
        UCP_OP_ATTR_FIELD_USER_DATA;
    params.cb.send = send_complete;
    params.user_data = state.get();
    if (state->memory != nullptr) {
        params.op_attr_mask |= UCP_OP_ATTR_FIELD_MEMH;
        params.memh = state->memory->handle;
    }

    void* request = ::ucp_tag_send_nbx(
        impl_->endpoint,
        buffer.data(),
        buffer.size(),
        tag,
        &params);
    if (UCS_PTR_IS_ERR(request)) {
        throw_ucx("ucp_tag_send_nbx", UCS_PTR_STATUS(request));
    }
    if (request == nullptr) {
        state->status = UCS_OK;
        state->complete = true;
    } else {
        state->native_request = request;
        impl_->register_request(state.get());
    }
    return UCXRequest{std::move(state)};
}

UCXRequest UCXTransport::receive(
    std::span<std::byte> buffer,
    std::uint64_t tag,
    std::uint64_t tag_mask,
    const UCXMemoryRegion* memory) {
    if (impl_ == nullptr || impl_->endpoint == nullptr) {
        throw std::logic_error("UCX transport is not connected");
    }
    if (buffer.empty()) {
        throw std::invalid_argument("UCX receive buffer must not be empty");
    }
    impl_->check_endpoint();

    auto state = std::make_shared<UCXRequest::State>();
    state->owner = impl_;
    state->receive = true;
    state->memory = checked_memory(
        impl_,
        memory == nullptr ? nullptr : memory->state_,
        buffer.data(),
        buffer.size());

    ucp_request_param_t params {};
    params.op_attr_mask =
        UCP_OP_ATTR_FIELD_CALLBACK |
        UCP_OP_ATTR_FIELD_USER_DATA |
        UCP_OP_ATTR_FIELD_RECV_INFO;
    params.cb.recv = receive_complete;
    params.user_data = state.get();
    ucp_tag_recv_info_t immediate_info {};
    params.recv_info.tag_info = &immediate_info;
    if (state->memory != nullptr) {
        params.op_attr_mask |= UCP_OP_ATTR_FIELD_MEMH;
        params.memh = state->memory->handle;
    }

    void* request = ::ucp_tag_recv_nbx(
        impl_->worker,
        buffer.data(),
        buffer.size(),
        tag,
        tag_mask,
        &params);
    if (UCS_PTR_IS_ERR(request)) {
        throw_ucx("ucp_tag_recv_nbx", UCS_PTR_STATUS(request));
    }
    if (request == nullptr) {
        state->status = UCS_OK;
        state->bytes = immediate_info.length;
        state->complete = true;
    } else {
        state->native_request = request;
        impl_->register_request(state.get());
    }
    return UCXRequest{std::move(state)};
}

UCXMemoryRegion UCXTransport::register_memory(
    std::span<std::byte> memory) {
    if (impl_ == nullptr || impl_->context == nullptr) {
        throw std::logic_error("UCX transport is not initialized");
    }
    if (memory.empty()) {
        throw std::invalid_argument(
            "UCX memory region must not be empty");
    }

    ucp_mem_map_params_t params {};
    params.field_mask =
        UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
        UCP_MEM_MAP_PARAM_FIELD_LENGTH;
    params.address = memory.data();
    params.length = memory.size();

    ucp_mem_h handle = nullptr;
    const ucs_status_t status =
        ::ucp_mem_map(impl_->context, &params, &handle);
    if (status != UCS_OK) {
        throw_ucx("ucp_mem_map", status);
    }

    auto state = std::make_shared<UCXMemoryRegion::State>();
    state->owner = impl_;
    state->handle = handle;
    state->address = memory.data();
    state->length = memory.size();
    return UCXMemoryRegion{std::move(state)};
}

bool UCXTransport::progress() {
    if (impl_ == nullptr || impl_->worker == nullptr) {
        throw std::logic_error("UCX transport is not initialized");
    }
    impl_->check_endpoint();
    return ::ucp_worker_progress(impl_->worker) != 0;
}

void UCXTransport::wait(
    UCXRequest& request,
    std::chrono::milliseconds timeout) {
    if (impl_ == nullptr || request.state_ == nullptr) {
        throw std::invalid_argument("invalid UCX request");
    }
    const auto owner = request.state_->owner.lock();
    if (owner.get() != impl_.get()) {
        throw std::invalid_argument("UCX request belongs to another transport");
    }

    progress_until(
        *impl_,
        [&request] { return request.state_->complete; },
        timeout,
        "wait for UCX request");

    impl_->release_request(*request.state_, false);
    if (request.state_->status != UCS_OK) {
        throw_ucx("UCX request", request.state_->status);
    }
}

}  // namespace sidecar::network
