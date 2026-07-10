#ifndef CYCORE_BLOCK_H
#define CYCORE_BLOCK_H

#include <string>
#include <string_view>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <vector>
#include <stdexcept>
#include <utility>

#include "lifecycle.h"
#include "port.h"

namespace cy::flowgraph {


namespace detail {

inline std::vector<std::string> split_names() {
    return {};
}

// 辅助函数：在运行时切分并清洗宏传进来的变量名称字符串
inline std::vector<std::string> split_names(std::string_view names_str) {
    std::vector<std::string> result;
    std::string item;
    std::istringstream ss{std::string(names_str)};
    while (std::getline(ss, item, ',')) {
        auto start = item.find_first_not_of(" \t\r\n");
        auto end   = item.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
            result.push_back(item.substr(start, end - start + 1));
        }
    }
    return result;
}

// 辅助函数：为 PortBase 派生类绑定反射名称（非 Port 类型的成员自动跳过）
template <typename Tuple, std::size_t... Is>
void bind_names_impl(Tuple& tuple, const std::vector<std::string>& names,
                     std::index_sequence<Is...>) {
    auto bind_one = [](auto& member, const std::string& name) {
        using MemberType = std::decay_t<decltype(member)>;
        if constexpr (std::is_base_of_v<PortBase, MemberType>) {
            member.set_name(name);
        }
    };
    (bind_one(std::get<Is>(tuple), names[Is]), ...);
}

template <typename Tuple>
void bind_names(Tuple& tuple, const std::vector<std::string>& names) {
    constexpr std::size_t tuple_size = std::tuple_size_v<std::decay_t<Tuple>>;
    if (names.size() != tuple_size) {
        throw std::runtime_error("Reflection name count mismatch: expected " +
                                 std::to_string(tuple_size) + ", got " +
                                 std::to_string(names.size()));
    }
    bind_names_impl(tuple, names,
                    std::make_index_sequence<tuple_size>{});
}

// C++17 编译期特征检测结构体：检测 Derived 是否定义了 cy_reflect_members
template <typename T, typename = std::void_t<>>
struct has_cy_reflect_members : std::false_type {};

template <typename T>
struct has_cy_reflect_members<T, std::void_t<decltype(std::declval<T>().cy_reflect_members())>> : std::true_type {};

} // namespace detail

// CRTP 算子基类
template <typename Derived>
class Block {
public:
    Block() = default;
    virtual ~Block() = default;

    // ─── 生命周期管理 ───────────────────────────────────────────────
    // 基类提供状态转换控制，具体行为由派生类可选地实现同名方法来覆盖

    void init() {
        if constexpr (detail::has_cy_reflect_members<Derived>::value) {
            static_cast<Derived*>(this)->cy_reflect_members();
        }
        static_cast<Derived*>(this)->on_init();
        state_ = lifecycle::State::INITIALISED;
    }

    void start() {
        static_cast<Derived*>(this)->on_start();
        state_ = lifecycle::State::RUNNING;
    }

    void stop() {
        static_cast<Derived*>(this)->on_stop();
        state_ = lifecycle::State::STOPPED;
    }

    void pause() {
        static_cast<Derived*>(this)->on_pause();
        state_ = lifecycle::State::PAUSED;
    }

    void resume() {
        static_cast<Derived*>(this)->on_resume();
        state_ = lifecycle::State::RUNNING;
    }

    void work() {
        static_cast<Derived*>(this)->process_work();
    }

    lifecycle::State state() const noexcept { return state_; }

protected:
    // 派生类可按需重写的默认空实现钩子
    void on_init()      {}
    void on_start()     {}
    void on_stop()      {}
    void on_pause()     {}
    void on_resume()    {}
    void process_work() {}

private:
    lifecycle::State state_ = lifecycle::State::IDLE;
};

} // namespace cy::flowgraph

// C++17 静态反射宏
// 将 Block 的端口与参数成员绑定到运行时可查询的反射元数据中
#define CY_MAKE_REFLECTABLE(ClassName, ...)                                     \
    static const std::vector<std::string>& cy_reflect_names() {                 \
        static const auto names =                                               \
            cy::flowgraph::detail::split_names(#__VA_ARGS__);                   \
        return names;                                                           \
    }                                                                           \
    auto cy_reflect_members() {                                                 \
        auto t = std::tie(__VA_ARGS__);                                         \
        cy::flowgraph::detail::bind_names(t, cy_reflect_names());               \
        return t;                                                               \
    }

#endif // CYCORE_BLOCK_H
