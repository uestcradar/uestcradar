#ifndef CYCORE_REGISTRY_H
#define CYCORE_REGISTRY_H

#include "block_wrapper.h"
#include "value.h"

#include <common/i_execution_context.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cy::flowgraph {

class RegistryError : public std::runtime_error {
public:
    explicit RegistryError(const std::string& message) : std::runtime_error(message) {}
};

struct BlockMetadata {
    std::string key;
    std::string type_name;
    std::string description;
    std::string category;
};

using BlockFactory = std::function<std::unique_ptr<BlockModel>(std::string, const ValueMap&)>;
using ContextBlockFactory =
    std::function<std::unique_ptr<BlockModel>(std::string, const ValueMap&, cy::common::IExecutionContext*)>;

class BlockRegistry {
public:
    BlockRegistry() = default;

    template <typename TBlock>
    void register_block(std::string key, BlockMetadata metadata = {}) {
        const std::string stable_key = checked_key(key);
        if (metadata.key.empty()) {
            metadata.key = stable_key;
        }
        if (metadata.key != stable_key) {
            throw RegistryError("Block metadata key does not match registry key: " + stable_key);
        }
        if (metadata.type_name.empty()) {
            metadata.type_name = stable_key;
        }

        const std::string type_name = metadata.type_name;
        register_context_factory(
            stable_key,
            [type_name](std::string instance_name,
                        const ValueMap& params,
                        cy::common::IExecutionContext* context) {
                return construct_block<TBlock>(std::move(instance_name), type_name, params, context);
            },
            std::move(metadata));
    }

    void register_factory(std::string key, BlockFactory factory, BlockMetadata metadata = {}) {
        if (!factory) {
            throw RegistryError("Block factory is empty for key: " + key);
        }
        register_context_factory(
            std::move(key),
            [factory = std::move(factory)](std::string instance_name,
                                           const ValueMap& params,
                                           cy::common::IExecutionContext*) {
                return factory(std::move(instance_name), params);
            },
            std::move(metadata));
    }

    void register_context_factory(std::string key, ContextBlockFactory factory, BlockMetadata metadata = {}) {
        const std::string stable_key = checked_key(key);
        if (!factory) {
            throw RegistryError("Block factory is empty for key: " + stable_key);
        }
        if (entries_.find(stable_key) != entries_.end()) {
            throw RegistryError("Block key already registered: " + stable_key);
        }
        if (metadata.key.empty()) {
            metadata.key = stable_key;
        }
        if (metadata.key != stable_key) {
            throw RegistryError("Block metadata key does not match registry key: " + stable_key);
        }
        if (metadata.type_name.empty()) {
            metadata.type_name = stable_key;
        }

        entries_.emplace(stable_key, Entry{std::move(metadata), std::move(factory)});
    }

    bool contains(std::string_view key) const {
        return entries_.find(std::string(key)) != entries_.end();
    }

    std::unique_ptr<BlockModel> create_block(std::string_view key,
                                             std::string instance_name,
                                             const ValueMap& params = {},
                                             cy::common::IExecutionContext* context = nullptr) const {
        const auto it = entries_.find(std::string(key));
        if (it == entries_.end()) {
            throw RegistryError("Unknown block key: " + std::string(key));
        }
        std::unique_ptr<BlockModel> block = it->second.factory(std::move(instance_name), params, context);
        if (!block) {
            throw RegistryError("Block factory returned null for key: " + std::string(key));
        }
        return block;
    }

    const BlockMetadata& metadata(std::string_view key) const {
        const auto it = entries_.find(std::string(key));
        if (it == entries_.end()) {
            throw RegistryError("Unknown block key: " + std::string(key));
        }
        return it->second.metadata;
    }

    std::vector<BlockMetadata> blocks() const {
        std::vector<BlockMetadata> result;
        result.reserve(entries_.size());
        for (const auto& entry : entries_) {
            result.push_back(entry.second.metadata);
        }
        return result;
    }

    bool empty() const noexcept {
        return entries_.empty();
    }

    std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    struct Entry {
        BlockMetadata metadata;
        ContextBlockFactory factory;
    };

    template <typename>
    struct always_false : std::false_type {};

    static std::string checked_key(const std::string& key) {
        if (key.empty()) {
            throw RegistryError("Block key must not be empty");
        }
        return key;
    }

    template <typename TBlock>
    static std::unique_ptr<BlockModel> construct_block(std::string instance_name,
                                                       const std::string& type_name,
                                                       const ValueMap& params,
                                                       cy::common::IExecutionContext* context) {
        static_assert(std::is_base_of<Block<TBlock>, TBlock>::value,
                      "register_block<TBlock> expects TBlock to inherit Block<TBlock>");

        if constexpr (std::is_constructible<TBlock, const ValueMap&, cy::common::IExecutionContext*>::value) {
            return std::unique_ptr<BlockModel>(
                new BlockWrapper<TBlock>(std::move(instance_name), BlockTypeName{type_name}, params, context));
        } else if constexpr (std::is_constructible<TBlock, const ValueMap&>::value) {
            return std::unique_ptr<BlockModel>(
                new BlockWrapper<TBlock>(std::move(instance_name), BlockTypeName{type_name}, params));
        } else if constexpr (std::is_constructible<TBlock, ValueMap, cy::common::IExecutionContext*>::value) {
            return std::unique_ptr<BlockModel>(
                new BlockWrapper<TBlock>(std::move(instance_name), BlockTypeName{type_name}, params, context));
        } else if constexpr (std::is_constructible<TBlock, ValueMap>::value) {
            return std::unique_ptr<BlockModel>(
                new BlockWrapper<TBlock>(std::move(instance_name), BlockTypeName{type_name}, params));
        } else if constexpr (std::is_default_constructible<TBlock>::value) {
            return std::unique_ptr<BlockModel>(
                new BlockWrapper<TBlock>(std::move(instance_name), BlockTypeName{type_name}));
        } else {
            static_assert(always_false<TBlock>::value,
                          "registered block must be default constructible or constructible from ValueMap");
        }
    }

    std::unordered_map<std::string, Entry> entries_;
};

} // namespace cy::flowgraph

#endif // CYCORE_REGISTRY_H
