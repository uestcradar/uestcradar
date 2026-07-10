#ifndef CYCORE_PLUGIN_H
#define CYCORE_PLUGIN_H

#include "registry.h"

#include <cstdint>
#include <string>
#include <utility>

namespace cy::flowgraph {

constexpr std::uint32_t CY_FLOWGRAPH_PLUGIN_ABI_VERSION = 2;

struct PluginMetadata {
    const char* name = "";
    const char* version = "";
    const char* description = "";
    const char* author = "";
};

class Plugin {
public:
    Plugin() = default;
    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;
    virtual ~Plugin() = default;

    virtual std::uint32_t abi_version() const noexcept = 0;
    virtual const PluginMetadata& metadata() const noexcept = 0;
    virtual BlockRegistry& block_registry() noexcept = 0;
    virtual const BlockRegistry& block_registry() const noexcept = 0;
};

class SimplePlugin : public Plugin {
public:
    explicit SimplePlugin(PluginMetadata metadata) : metadata_(metadata) {}

    std::uint32_t abi_version() const noexcept override {
        return CY_FLOWGRAPH_PLUGIN_ABI_VERSION;
    }

    const PluginMetadata& metadata() const noexcept override {
        return metadata_;
    }

    BlockRegistry& block_registry() noexcept override {
        return registry_;
    }

    const BlockRegistry& block_registry() const noexcept override {
        return registry_;
    }

private:
    PluginMetadata metadata_;
    BlockRegistry registry_;
};

using PluginMakeFn = Plugin* (*)();
using PluginFreeFn = void (*)(Plugin*);

} // namespace cy::flowgraph

#if defined(_WIN32)
#define CY_FLOWGRAPH_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CY_FLOWGRAPH_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define CY_REGISTER_BLOCK(block_key, BlockType) \
    plugin.block_registry().register_block<BlockType>(block_key);

#define CY_REGISTER_BLOCK_CATEGORY(block_key, BlockType, block_category) \
    plugin.block_registry().register_block<BlockType>( \
        block_key, ::cy::flowgraph::BlockMetadata{block_key, block_key, "", block_category});

#define CY_PLUGIN(plugin_name, plugin_version, plugin_description, plugin_author, ...)                 \
    namespace {                                                                                       \
    class CyGeneratedPlugin final : public ::cy::flowgraph::SimplePlugin {                            \
    public:                                                                                           \
        CyGeneratedPlugin()                                                                           \
            : ::cy::flowgraph::SimplePlugin(                                                          \
                  ::cy::flowgraph::PluginMetadata{plugin_name, plugin_version,                         \
                                                  plugin_description, plugin_author}) {                \
            auto& plugin = *this;                                                                     \
            (void)plugin;                                                                             \
            __VA_ARGS__                                                                               \
        }                                                                                             \
    };                                                                                                \
    }                                                                                                 \
    extern "C" CY_FLOWGRAPH_PLUGIN_EXPORT ::cy::flowgraph::Plugin* cy_plugin_make() {                 \
        return new CyGeneratedPlugin();                                                               \
    }                                                                                                 \
    extern "C" CY_FLOWGRAPH_PLUGIN_EXPORT void cy_plugin_free(::cy::flowgraph::Plugin* plugin) {      \
        delete plugin;                                                                                \
    }

#endif // CYCORE_PLUGIN_H
