#ifndef CYCORE_PLUGIN_LOADER_H
#define CYCORE_PLUGIN_LOADER_H

#include "plugin.h"

#include <common/i_execution_context.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cy::flowgraph {

class PluginError : public std::runtime_error {
public:
    explicit PluginError(const std::string& message) : std::runtime_error(message) {}
};

struct LoadedPluginInfo {
    std::string path;
    std::uint32_t abi_version = 0;
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::vector<BlockMetadata> blocks;
};

class PluginLoader {
public:
    PluginLoader() = default;
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;
    PluginLoader(PluginLoader&&) noexcept;
    PluginLoader& operator=(PluginLoader&&) noexcept;
    ~PluginLoader() noexcept;

    const LoadedPluginInfo& load(const std::string& path);
    std::size_t load_directory(const std::string& directory);

    std::shared_ptr<BlockModel> create_block(std::string_view key,
                                             std::string instance_name,
                                             const ValueMap& params = {},
                                             cy::common::IExecutionContext* context = nullptr) const;

    bool contains_block(std::string_view key) const;
    std::vector<LoadedPluginInfo> plugins() const;

    void unload_all();
    std::size_t size() const noexcept;

private:
    struct LoadedPlugin;

    std::shared_ptr<LoadedPlugin> find_plugin_for_block(std::string_view key) const;
    void check_duplicate_blocks(const LoadedPlugin& candidate) const;

    std::vector<std::shared_ptr<LoadedPlugin>> plugins_;
};

} // namespace cy::flowgraph

#endif // CYCORE_PLUGIN_LOADER_H
