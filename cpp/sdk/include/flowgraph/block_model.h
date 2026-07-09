#ifndef CYCORE_BLOCK_MODEL_H
#define CYCORE_BLOCK_MODEL_H

#include "block.h"

#include <string_view>
#include <vector>

namespace cy::flowgraph {

struct RuntimePort {
    PortBase* port = nullptr;
};

class BlockModel {
public:
    BlockModel() = default;
    BlockModel(const BlockModel&) = delete;
    BlockModel& operator=(const BlockModel&) = delete;
    virtual ~BlockModel() = default;

    virtual std::string_view instance_name() const noexcept = 0;
    virtual std::string_view type_name() const noexcept = 0;
    virtual lifecycle::State state() const noexcept = 0;

    virtual void init() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void work() = 0;

    virtual void* raw_block() noexcept = 0;
    virtual const void* raw_block() const noexcept = 0;

    virtual const std::vector<PortBase*>& ports() const noexcept = 0;
    virtual const std::vector<PortBase*>& input_ports() const noexcept = 0;
    virtual const std::vector<PortBase*>& output_ports() const noexcept = 0;

    virtual PortBase* input_port(std::string_view name) noexcept = 0;
    virtual PortBase* output_port(std::string_view name) noexcept = 0;
};

} // namespace cy::flowgraph

#endif // CYCORE_BLOCK_MODEL_H
