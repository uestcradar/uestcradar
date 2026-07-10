#ifndef CYCORE_VALUE_H
#define CYCORE_VALUE_H

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <variant>

namespace cy::flowgraph {

using Value = std::variant<bool, std::int64_t, double, std::string>;
using ValueMap = std::map<std::string, Value>;

class ValueError : public std::runtime_error {
public:
    explicit ValueError(const std::string& message) : std::runtime_error(message) {}
};

inline const Value* find_value(const ValueMap& values, std::string_view key) {
    const auto it = values.find(std::string(key));
    if (it == values.end()) {
        return nullptr;
    }
    return &it->second;
}

template <typename T>
bool value_is(const Value& value) noexcept {
    return std::holds_alternative<T>(value);
}

template <typename T>
const T& value_as(const Value& value) {
    if (const auto* typed = std::get_if<T>(&value)) {
        return *typed;
    }
    throw ValueError("Value type mismatch: requested " + std::string(typeid(T).name()));
}

template <typename T>
T& value_as(Value& value) {
    if (auto* typed = std::get_if<T>(&value)) {
        return *typed;
    }
    throw ValueError("Value type mismatch: requested " + std::string(typeid(T).name()));
}

template <typename T>
const T& value_at(const ValueMap& values, std::string_view key) {
    const Value* value = find_value(values, key);
    if (!value) {
        throw ValueError("Missing value for key: " + std::string(key));
    }
    return value_as<T>(*value);
}

template <typename T>
T value_or(const ValueMap& values, std::string_view key, T fallback) {
    const Value* value = find_value(values, key);
    if (!value) {
        return fallback;
    }
    return value_as<T>(*value);
}

} // namespace cy::flowgraph

#endif // CYCORE_VALUE_H
