#include "ringbuf.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace {

[[noreturn]] void throw_system_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (value > std::numeric_limits<std::size_t>::max() -
                    (alignment - 1)) {
        throw std::invalid_argument("ring slot size overflows");
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

struct Layout {
    std::size_t stride;
    std::size_t mapping_size;
};

Layout checked_layout(const RingBufferConfig& config) {
    if (config.slot_count < 2 ||
        config.max_payload_bytes == 0 ||
        config.max_payload_bytes >
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()) ||
        config.type_id == 0 || config.type_version == 0) {
        throw std::invalid_argument("invalid fixed-slot ring configuration");
    }
    const std::size_t stride = align_up(
        kSlotHeaderSize + config.max_payload_bytes,
        kSlotHeaderSize);
    if (stride >
        (std::numeric_limits<std::size_t>::max() - kRingHeaderSize) /
            config.slot_count) {
        throw std::invalid_argument("ring mapping size overflows");
    }
    const std::size_t mapping_size =
        kRingHeaderSize + stride * config.slot_count;
    if (mapping_size >
        static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
        throw std::invalid_argument("ring mapping is too large");
    }
    return {stride, mapping_size};
}

void* map_ring(int fd, std::size_t mapping_size) {
    void* address = ::mmap(
        nullptr,
        mapping_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0);
    if (address == MAP_FAILED) {
        throw_system_error("mmap");
    }
    return address;
}

RingBuffer* make_handle(void* address, std::size_t mapping_size) {
    try {
        return new RingBuffer{
            static_cast<RingBufferHeader*>(address),
            static_cast<std::byte*>(address) + kRingHeaderSize,
            mapping_size,
        };
    } catch (...) {
        ::munmap(address, mapping_size);
        throw;
    }
}

RingSlotHeader* slot_header(
    RingBuffer* ring,
    std::uint64_t position) noexcept {
    const std::size_t index =
        static_cast<std::size_t>(position % ring->header->slot_count);
    return reinterpret_cast<RingSlotHeader*>(
        ring->slots + index * ring->header->slot_stride);
}

std::byte* slot_payload(RingSlotHeader* slot) noexcept {
    return reinterpret_cast<std::byte*>(slot) + kSlotHeaderSize;
}

const std::byte* slot_payload(const RingSlotHeader* slot) noexcept {
    return reinterpret_cast<const std::byte*>(slot) + kSlotHeaderSize;
}

}  // namespace

void RingWriteLease::reset() noexcept {
    ring_ = nullptr;
    position_ = 0;
    payload_ = nullptr;
    envelope_ = nullptr;
    capacity_ = 0;
}

RingWriteLease::RingWriteLease(RingWriteLease&& other) noexcept {
    *this = std::move(other);
}

RingWriteLease& RingWriteLease::operator=(
    RingWriteLease&& other) noexcept {
    if (this != &other) {
        ringbuf_cancel(*this);
        ring_ = other.ring_;
        position_ = other.position_;
        payload_ = other.payload_;
        envelope_ = other.envelope_;
        capacity_ = other.capacity_;
        other.reset();
    }
    return *this;
}

std::span<std::byte> RingWriteLease::payload() const noexcept {
    return {payload_, capacity_};
}

uestcradar::Envelope& RingWriteLease::envelope() const noexcept {
    return *envelope_;
}

std::span<std::byte> RingWriteLease::frame_capacity() const noexcept {
    return envelope_ == nullptr
               ? std::span<std::byte>{}
               : std::span<std::byte>{
                     reinterpret_cast<std::byte*>(envelope_),
                     kSlotHeaderSize + capacity_};
}

bool RingWriteLease::active() const noexcept {
    return ring_ != nullptr;
}

void RingReadLease::reset() noexcept {
    ring_ = nullptr;
    position_ = 0;
    payload_ = nullptr;
    envelope_ = nullptr;
    length_ = 0;
}

RingReadLease::RingReadLease(RingReadLease&& other) noexcept {
    *this = std::move(other);
}

RingReadLease& RingReadLease::operator=(
    RingReadLease&& other) noexcept {
    if (this != &other) {
        ring_ = other.ring_;
        position_ = other.position_;
        payload_ = other.payload_;
        envelope_ = other.envelope_;
        length_ = other.length_;
        other.reset();
    }
    return *this;
}

std::span<const std::byte> RingReadLease::payload() const noexcept {
    return {payload_, length_};
}

const uestcradar::Envelope& RingReadLease::envelope() const noexcept {
    return *envelope_;
}

std::span<const std::byte> RingReadLease::frame() const noexcept {
    return envelope_ == nullptr
               ? std::span<const std::byte>{}
               : std::span<const std::byte>{
                     reinterpret_cast<const std::byte*>(envelope_),
                     kSlotHeaderSize + length_};
}

bool RingReadLease::active() const noexcept {
    return ring_ != nullptr;
}

RingBuffer* ringbuf_create(
    const char* name,
    const RingBufferConfig& config) {
    if (name == nullptr || name[0] == '\0') {
        throw std::invalid_argument("ring name must not be empty");
    }
    const Layout layout = checked_layout(config);

    if (::shm_unlink(name) == -1 && errno != ENOENT) {
        throw_system_error("shm_unlink");
    }
    const int fd = ::shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1) {
        throw_system_error("shm_open(create)");
    }
    if (::ftruncate(fd, static_cast<off_t>(layout.mapping_size)) == -1) {
        const int saved_errno = errno;
        ::close(fd);
        ::shm_unlink(name);
        errno = saved_errno;
        throw_system_error("ftruncate");
    }

    void* address = nullptr;
    try {
        address = map_ring(fd, layout.mapping_size);
    } catch (...) {
        ::close(fd);
        ::shm_unlink(name);
        throw;
    }
    ::close(fd);

    auto* header = ::new (address) RingBufferHeader{};
    header->slot_count = config.slot_count;
    header->slot_stride = layout.stride;
    header->max_payload_bytes = config.max_payload_bytes;
    header->type_id = config.type_id;
    header->type_version = config.type_version;
    header->magic.store(kRingMagic, std::memory_order_release);
    return make_handle(address, layout.mapping_size);
}

RingBuffer* ringbuf_open(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        throw std::invalid_argument("ring name must not be empty");
    }
    for (;;) {
        const int fd = ::shm_open(name, O_RDWR, 0);
        if (fd == -1) {
            if (errno == ENOENT) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                continue;
            }
            throw_system_error("shm_open(open)");
        }
        struct stat status {};
        if (::fstat(fd, &status) == -1) {
            const int saved_errno = errno;
            ::close(fd);
            errno = saved_errno;
            throw_system_error("fstat");
        }
        if (status.st_size < static_cast<off_t>(kRingHeaderSize)) {
            ::close(fd);
            throw std::runtime_error("ring mapping is shorter than header");
        }
        const std::size_t mapping_size =
            static_cast<std::size_t>(status.st_size);
        void* address = nullptr;
        try {
            address = map_ring(fd, mapping_size);
        } catch (...) {
            ::close(fd);
            throw;
        }
        ::close(fd);

        auto* header = static_cast<RingBufferHeader*>(address);
        while (header->magic.load(std::memory_order_acquire) !=
               kRingMagic) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        bool valid =
            header->abi_version == kRingAbiVersion &&
            header->header_size == kRingHeaderSize &&
            header->slot_header_size == kSlotHeaderSize &&
            header->slot_count >= 2 &&
            header->max_payload_bytes > 0 &&
            header->max_payload_bytes <=
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max()) &&
            header->type_id != 0 && header->type_version != 0;
        if (valid) {
            const std::uint64_t expected_stride =
                (kSlotHeaderSize + header->max_payload_bytes +
                 kSlotHeaderSize - 1) &
                ~(static_cast<std::uint64_t>(kSlotHeaderSize) - 1);
            valid =
                header->slot_stride == expected_stride &&
                expected_stride <=
                    (std::numeric_limits<std::size_t>::max() -
                     kRingHeaderSize) /
                        header->slot_count &&
                mapping_size ==
                    kRingHeaderSize +
                        static_cast<std::size_t>(expected_stride) *
                            header->slot_count;
        }
        if (!valid) {
            ::munmap(address, mapping_size);
            throw std::runtime_error("ringbuf ABI mismatch");
        }
        return make_handle(address, mapping_size);
    }
}

RingResult ringbuf_reserve(
    RingBuffer* ring,
    RingWriteLease& lease) noexcept {
    if (lease.active() || ring == nullptr || ring->header == nullptr) {
        return RingResult::invalid_argument;
    }
    if (ringbuf_is_shutdown(ring)) {
        return RingResult::shutdown;
    }
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_relaxed);
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_acquire);
    if (write < read || write - read > ring->header->slot_count) {
        return RingResult::corrupt;
    }
    if (write - read == ring->header->slot_count) {
        return RingResult::would_block;
    }
    RingSlotHeader* slot = slot_header(ring, write);
    ::new (slot) uestcradar::Envelope{};
    lease.ring_ = ring;
    lease.position_ = write;
    lease.envelope_ = slot;
    lease.payload_ = slot_payload(slot);
    lease.capacity_ =
        static_cast<std::size_t>(ring->header->max_payload_bytes);
    return RingResult::ok;
}

RingResult ringbuf_commit(
    RingWriteLease& lease) noexcept {
    if (!lease.active()) {
        return RingResult::invalid_argument;
    }
    RingBuffer* ring = lease.ring_;
    const uestcradar::Envelope& envelope = lease.envelope();
    if (envelope.type_id != ring->header->type_id ||
        envelope.type_version != ring->header->type_version ||
        envelope.payload_length == 0 ||
        envelope.payload_length > lease.capacity_) {
        return RingResult::invalid_argument;
    }
    if (ringbuf_is_shutdown(ring)) {
        return RingResult::shutdown;
    }
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_relaxed);
    if (write != lease.position_) {
        return RingResult::corrupt;
    }
    ring->header->write_position.store(write + 1, std::memory_order_release);
    lease.reset();
    return RingResult::ok;
}

void ringbuf_cancel(RingWriteLease& lease) noexcept {
    lease.reset();
}

RingResult ringbuf_acquire(
    RingBuffer* ring,
    RingReadLease& lease) noexcept {
    if (lease.active() || ring == nullptr || ring->header == nullptr) {
        return RingResult::invalid_argument;
    }
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_relaxed);
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_acquire);
    if (write < read || write - read > ring->header->slot_count) {
        return RingResult::corrupt;
    }
    if (write == read) {
        return ringbuf_is_shutdown(ring)
                   ? RingResult::shutdown
                   : RingResult::would_block;
    }
    const RingSlotHeader* slot = slot_header(ring, read);
    const std::size_t length = slot->payload_length;
    if (slot->type_id != ring->header->type_id ||
        slot->type_version != ring->header->type_version ||
        length == 0 || length > ring->header->max_payload_bytes) {
        return RingResult::corrupt;
    }
    lease.ring_ = ring;
    lease.position_ = read;
    lease.envelope_ = slot;
    lease.payload_ = slot_payload(slot);
    lease.length_ = length;
    return RingResult::ok;
}

RingResult ringbuf_release(RingReadLease& lease) noexcept {
    if (!lease.active()) {
        return RingResult::invalid_argument;
    }
    RingBuffer* ring = lease.ring_;
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_relaxed);
    if (read != lease.position_) {
        return RingResult::corrupt;
    }
    ring->header->read_position.store(read + 1, std::memory_order_release);
    lease.reset();
    return RingResult::ok;
}

std::uint32_t ringbuf_slot_count(const RingBuffer* ring) noexcept {
    return ring == nullptr || ring->header == nullptr
               ? 0
               : ring->header->slot_count;
}

std::size_t ringbuf_max_payload_bytes(
    const RingBuffer* ring) noexcept {
    return ring == nullptr || ring->header == nullptr
               ? 0
               : static_cast<std::size_t>(
                     ring->header->max_payload_bytes);
}

std::size_t ringbuf_occupied_slots(
    const RingBuffer* ring) noexcept {
    if (ring == nullptr || ring->header == nullptr) {
        return 0;
    }
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_acquire);
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_acquire);
    return write >= read && write - read <= ring->header->slot_count
               ? static_cast<std::size_t>(write - read)
               : 0;
}

std::span<std::byte> ringbuf_storage(RingBuffer* ring) noexcept {
    return ring == nullptr || ring->header == nullptr
               ? std::span<std::byte>{}
               : std::span<std::byte>{
                     ring->slots,
                     ring->mapping_size - kRingHeaderSize};
}

bool ringbuf_is_shutdown(const RingBuffer* ring) noexcept {
    return ring == nullptr || ring->header == nullptr ||
           ring->header->shutdown.load(std::memory_order_acquire) != 0;
}

void ringbuf_shutdown(RingBuffer* ring) noexcept {
    if (ring != nullptr && ring->header != nullptr) {
        ring->header->shutdown.store(1, std::memory_order_release);
    }
}

void ringbuf_close(RingBuffer* ring) noexcept {
    if (ring == nullptr) {
        return;
    }
    if (ring->header != nullptr && ring->mapping_size != 0) {
        ::munmap(ring->header, ring->mapping_size);
    }
    delete ring;
}

void ringbuf_unlink(const char* name) noexcept {
    if (name != nullptr && name[0] != '\0') {
        ::shm_unlink(name);
    }
}
