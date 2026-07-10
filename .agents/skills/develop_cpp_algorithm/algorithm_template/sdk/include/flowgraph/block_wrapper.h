#ifndef CYCORE_BLOCK_WRAPPER_H
#define CYCORE_BLOCK_WRAPPER_H

#include "block_model.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace cy::flowgraph {

struct BlockTypeName {
    std::string value;
};

template <typename TBlock>
class BlockWrapper final : public BlockModel {
public:
    template <typename... Args>
    explicit BlockWrapper(std::string instance_name, Args&&... args)
        : BlockWrapper(std::move(instance_name),
                       BlockTypeName{typeid(TBlock).name()},
                       std::forward<Args>(args)...) {}

    template <typename... Args>
    explicit BlockWrapper(std::string instance_name, BlockTypeName type_name, Args&&... args)
        : block_(std::forward<Args>(args)...),
          instance_name_(std::move(instance_name)),
          type_name_(std::move(type_name.value)) {
        static_assert(std::is_base_of<Block<TBlock>, TBlock>::value,
                      "BlockWrapper<TBlock> expects TBlock to inherit Block<TBlock>");
        if (type_name_.empty()) {
            type_name_ = typeid(TBlock).name();
        }
        collect_ports();
    }

    TBlock& block() noexcept { return block_; }
    const TBlock& block() const noexcept { return block_; }

    std::string_view instance_name() const noexcept override { return instance_name_; }
    std::string_view type_name() const noexcept override { return type_name_; }
    lifecycle::State state() const noexcept override { return block_.state(); }

    void init() override { block_.init(); }
    void start() override { block_.start(); }
    void stop() override { block_.stop(); }
    void pause() override { block_.pause(); }
    void resume() override { block_.resume(); }
    void work() override { block_.work(); }

    void* raw_block() noexcept override { return &block_; }
    const void* raw_block() const noexcept override { return &block_; }

    const std::vector<PortBase*>& ports() const noexcept override { return ports_; }
    const std::vector<PortBase*>& input_ports() const noexcept override { return input_ports_; }
    const std::vector<PortBase*>& output_ports() const noexcept override { return output_ports_; }

    PortBase* input_port(std::string_view name) noexcept override {
        return find_port(input_ports_, name);
    }

    PortBase* output_port(std::string_view name) noexcept override {
        return find_port(output_ports_, name);
    }

private:
    void collect_ports() {
        ports_.clear();
        input_ports_.clear();
        output_ports_.clear();

        if constexpr (detail::has_cy_reflect_members<TBlock>::value) {
            auto members = block_.cy_reflect_members();
            collect_tuple_ports(members, std::make_index_sequence<std::tuple_size<decltype(members)>::value>{});
        }
    }

    template <typename Tuple, std::size_t... Is>
    void collect_tuple_ports(Tuple& members, std::index_sequence<Is...>) {
        (collect_member(std::get<Is>(members)), ...);
    }

    template <typename Member>
    void collect_member(Member& member) {
        using MemberType = typename std::decay<Member>::type;
        if constexpr (std::is_base_of<PortBase, MemberType>::value) {
            auto* port = static_cast<PortBase*>(&member);
            ports_.push_back(port);
            if (port->direction() == PortDirection::INPUT) {
                input_ports_.push_back(port);
            } else if (port->direction() == PortDirection::OUTPUT) {
                output_ports_.push_back(port);
            }
        }
    }

    static PortBase* find_port(const std::vector<PortBase*>& ports, std::string_view name) noexcept {
        for (PortBase* port : ports) {
            if (port && port->name() == name) {
                return port;
            }
        }
        return nullptr;
    }

    TBlock block_;
    std::string instance_name_;
    std::string type_name_;
    std::vector<PortBase*> ports_;
    std::vector<PortBase*> input_ports_;
    std::vector<PortBase*> output_ports_;
};

} // namespace cy::flowgraph

#endif // CYCORE_BLOCK_WRAPPER_H
