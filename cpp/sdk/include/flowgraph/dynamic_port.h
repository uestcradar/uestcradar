#ifndef CYCORE_DYNAMIC_PORT_H
#define CYCORE_DYNAMIC_PORT_H

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>

namespace cy::flowgraph {

enum class PortDirection { INPUT, OUTPUT };
enum class PortType { STREAM, MESSAGE };

class DynamicPort {
public:
    using SizeFn = std::function<std::size_t()>;
    using BoolFn = std::function<bool()>;

    DynamicPort() = default;

    DynamicPort(std::string_view name,
                PortDirection direction,
                PortType port_type,
                std::type_index type_info,
                std::string type_key,
                std::size_t item_size,
                std::size_t item_alignment,
                SizeFn available,
                BoolFn connected)
        : name_(name),
          direction_(direction),
          port_type_(port_type),
          type_info_(type_info),
          type_key_(std::move(type_key)),
          item_size_(item_size),
          item_alignment_(item_alignment),
          available_(std::move(available)),
          connected_(std::move(connected)) {}

    std::string_view name() const noexcept { return name_; }
    PortDirection direction() const noexcept { return direction_; }
    PortType port_type() const noexcept { return port_type_; }
    std::type_index type_info() const noexcept { return type_info_; }
    std::string_view type_key() const noexcept { return type_key_; }
    std::size_t item_size() const noexcept { return item_size_; }
    std::size_t item_alignment() const noexcept { return item_alignment_; }

    std::size_t available() const { return available_ ? available_() : 0; }
    bool connected() const { return connected_ ? connected_() : false; }

private:
    std::string name_;
    PortDirection direction_ = PortDirection::INPUT;
    PortType port_type_ = PortType::STREAM;
    std::type_index type_info_{typeid(void)};
    std::string type_key_;
    std::size_t item_size_ = 0;
    std::size_t item_alignment_ = 0;
    SizeFn available_;
    BoolFn connected_;
};

} // namespace cy::flowgraph

#endif // CYCORE_DYNAMIC_PORT_H
