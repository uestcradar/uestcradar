#ifndef CYCORE_PORT_H
#define CYCORE_PORT_H

#include "circular_buffer.h"
#include "dynamic_port.h"
#include "probe.h"
#include "span.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace cy::flowgraph {

template <typename T>
class PortIn;

template <typename T>
class PortOut;

template <typename OutT, typename InT>
void connect(PortOut<OutT>& source, PortIn<InT>& destination, std::size_t capacity = 1024);

namespace detail {

template <typename T>
std::string type_key() {
    using U = typename std::remove_cv<T>::type;
    if constexpr (std::is_same<U, bool>::value) {
        return "bool";
    } else if constexpr (std::is_same<U, char>::value) {
        return "char";
    } else if constexpr (std::is_same<U, std::int8_t>::value) {
        return "int8";
    } else if constexpr (std::is_same<U, std::uint8_t>::value) {
        return "uint8";
    } else if constexpr (std::is_same<U, std::int16_t>::value) {
        return "int16";
    } else if constexpr (std::is_same<U, std::uint16_t>::value) {
        return "uint16";
    } else if constexpr (std::is_same<U, std::int32_t>::value) {
        return "int32";
    } else if constexpr (std::is_same<U, std::uint32_t>::value) {
        return "uint32";
    } else if constexpr (std::is_same<U, std::int64_t>::value) {
        return "int64";
    } else if constexpr (std::is_same<U, std::uint64_t>::value) {
        return "uint64";
    } else if constexpr (std::is_same<U, float>::value) {
        return "float32";
    } else if constexpr (std::is_same<U, double>::value) {
        return "float64";
    } else if constexpr (std::is_same<U, std::string>::value) {
        return "std::string";
    } else {
        return typeid(T).name();
    }
}

} // namespace detail

// 无模板的端口基类，为后期类型擦除与运行时反射查询提供标准虚接口
class PortBase {
public:
    PortBase(std::string_view name,
             PortDirection dir,
             PortType type,
             std::type_index type_idx,
             std::string type_key,
             std::size_t item_size,
             std::size_t item_alignment)
        : name_(name),
          direction_(dir),
          type_(type),
          type_info_(type_idx),
          type_key_(std::move(type_key)),
          item_size_(item_size),
          item_alignment_(item_alignment) {}
    virtual ~PortBase() = default;

    std::string_view name() const noexcept { return name_; }
    PortDirection direction() const noexcept { return direction_; }
    PortType port_type() const noexcept { return type_; }
    std::type_index type_info() const noexcept { return type_info_; }
    std::string_view type_key() const noexcept { return type_key_; }
    std::size_t item_size() const noexcept { return item_size_; }
    std::size_t item_alignment() const noexcept { return item_alignment_; }
    virtual std::size_t buffer_size() const noexcept { return 0; }

    virtual DynamicPort dynamic_port() = 0;
    virtual void connect_to(PortBase& destination, std::size_t capacity) = 0;
    virtual void disconnect() = 0;
    virtual bool attach_probe(std::shared_ptr<cy::common::IProbe> probe) {
        (void)probe;
        return false;
    }

protected:
    // 允许派生类在反射绑定时设置端口的物理变量名称
    void set_port_name(std::string_view name) { name_ = name; }

private:
    std::string name_;
    PortDirection direction_;
    PortType type_;
    std::type_index type_info_;
    std::string type_key_;
    std::size_t item_size_;
    std::size_t item_alignment_;
};

// 强类型输入端口
template <typename T>
class PortIn : public PortBase {
public:
    using value_type = T;
    using Reader = typename CircularBuffer<T>::Reader;

    PortIn()
        : PortBase("", PortDirection::INPUT, PortType::STREAM, typeid(T),
                   detail::type_key<T>(), sizeof(T), alignof(T)),
          value{} {}

    void set_name(std::string_view name) {
        this->set_port_name(name);
    }

    bool connected() const noexcept {
        return reader_.connected();
    }

    std::size_t available() const {
        return connected() ? reader_.available() : 0;
    }

    InputSpan<T> get(std::size_t n) {
        if (!connected()) {
            throw std::logic_error("PortIn is not connected");
        }
        return reader_.get(n);
    }

    DynamicPort dynamic_port() override {
        return DynamicPort(
            name(), direction(), port_type(), type_info(), std::string(type_key()),
            item_size(), item_alignment(),
            [this]() { return this->available(); },
            [this]() { return this->connected(); });
    }

    void connect_to(PortBase&, std::size_t) override {
        throw std::logic_error("PortIn cannot initiate a connection");
    }

    void disconnect() override {
        reader_.disconnect();
    }

    T value; // 数据传递临时中转区

private:
    template <typename OutT, typename InT>
    friend void connect(PortOut<OutT>& source, PortIn<InT>& destination, std::size_t capacity);

    void bind_reader(Reader reader) {
        if (connected()) {
            throw std::logic_error("PortIn is already connected");
        }
        reader_ = std::move(reader);
    }

    Reader reader_;
};

// 强类型输出端口
template <typename T>
class PortOut : public PortBase {
public:
    using value_type = T;
    using Buffer = CircularBuffer<T>;
    using Writer = typename CircularBuffer<T>::Writer;

    PortOut()
        : PortBase("", PortDirection::OUTPUT, PortType::STREAM, typeid(T),
                   detail::type_key<T>(), sizeof(T), alignof(T)),
          value{} {}

    void set_name(std::string_view name) {
        this->set_port_name(name);
    }

    bool connected() const noexcept {
        return writer_.connected();
    }

    std::size_t available() const {
        return connected() ? writer_.available() : 0;
    }

    std::size_t n_readers() const {
        return connected() ? writer_.n_readers() : 0;
    }

    std::size_t buffer_size() const noexcept override {
        return capacity_;
    }

    OutputSpan<T> reserve(std::size_t n) {
        if (!connected()) {
            throw std::logic_error("PortOut is not connected");
        }
        auto span = writer_.reserve(n);
        if (!span.empty() && probe_) {
            span.set_commit_probe(probe_, &Probe<T>::capture_latest_hook);
        }
        return span;
    }

    DynamicPort dynamic_port() override {
        return DynamicPort(
            name(), direction(), port_type(), type_info(), std::string(type_key()),
            item_size(), item_alignment(),
            [this]() { return this->available(); },
            [this]() { return this->connected(); });
    }

    void connect_to(PortBase& destination, std::size_t capacity) override {
        auto* typed_destination = dynamic_cast<PortIn<T>*>(&destination);
        if (!typed_destination) {
            throw std::invalid_argument("Cannot connect ports with different sample types");
        }
        connect(*this, *typed_destination, capacity);
    }

    void disconnect() override {
        if (writer_.connected()) {
            if (writer_.n_readers() != 0 || !writer_.idle()) {
                throw std::logic_error("PortOut cannot disconnect while readers or pending output spans exist");
            }
        }
        writer_ = Writer();
        buffer_.reset();
        capacity_ = 0;
    }

    bool attach_probe(std::shared_ptr<cy::common::IProbe> probe) override {
        auto typed_probe = std::dynamic_pointer_cast<Probe<T>>(std::move(probe));
        if (!typed_probe) {
            return false;
        }
        if (probe_) {
            return false;
        }
        probe_ = std::move(typed_probe);
        return true;
    }

    T value; // 数据传递临时中转区

private:
    template <typename OutT, typename InT>
    friend void connect(PortOut<OutT>& source, PortIn<InT>& destination, std::size_t capacity);

    Buffer& ensure_buffer(std::size_t capacity) {
        const std::size_t normalized_capacity = detail::next_power_of_two(capacity);
        if (!buffer_) {
            buffer_.reset(new Buffer(capacity));
            writer_ = buffer_->new_writer();
            capacity_ = buffer_->size();
        } else if (normalized_capacity != capacity_) {
            if (writer_.n_readers() != 0 || !writer_.idle()) {
                throw std::invalid_argument("PortOut is already connected with a different buffer capacity");
            }
            writer_ = Writer();
            buffer_.reset(new Buffer(capacity));
            writer_ = buffer_->new_writer();
            capacity_ = buffer_->size();
        }
        return *buffer_;
    }

    std::shared_ptr<Buffer> buffer_;
    Writer writer_;
    std::shared_ptr<Probe<T>> probe_;
    std::size_t capacity_ = 0;
};

template <typename OutT, typename InT>
void connect(PortOut<OutT>& source, PortIn<InT>& destination, std::size_t capacity) {
    if (capacity == 0) {
        throw std::invalid_argument("Port buffer capacity must be greater than zero");
    }
    if (destination.connected()) {
        throw std::logic_error("Destination PortIn is already connected");
    }

    if constexpr (!std::is_same<OutT, InT>::value) {
        (void)source;
        throw std::invalid_argument("Cannot connect ports with different sample types");
    } else {
        auto& buffer = source.ensure_buffer(capacity);
        destination.bind_reader(buffer.new_reader());
    }
}

} // namespace cy::flowgraph

#endif // CYCORE_PORT_H
