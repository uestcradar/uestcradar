#ifndef CYCORE_GRAPH_H
#define CYCORE_GRAPH_H

#include "block_wrapper.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cy::flowgraph {

class GraphError : public std::runtime_error {
public:
    explicit GraphError(const std::string& message) : std::runtime_error(message) {}
};

struct EdgeOptions {
    std::size_t capacity = 1024;
    std::string name;
};

enum class EdgeState {
    WaitingToConnect,
    Connected,
    PortNotFound,
    DirectionMismatch,
    TypeMismatch,
    DuplicateEdge,
    CycleRejected,
    ErrorConnecting
};

struct Edge {
    std::shared_ptr<BlockModel> source;
    std::shared_ptr<BlockModel> destination;
    std::string source_port;
    std::string destination_port;
    std::size_t requested_capacity = 1024;
    std::size_t actual_capacity = 0;
    EdgeState state = EdgeState::WaitingToConnect;
    std::string error_message;
    std::string name;
};

class Graph {
public:
    Graph() = default;
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

    template <typename TBlock, typename... Args>
    TBlock& emplace(std::string instance_name = {}, Args&&... args) {
        if (finalized_) {
            throw GraphError("Cannot add block after graph connections have been finalized");
        }

        const std::string final_name = instance_name.empty() ? next_instance_name() : std::move(instance_name);
        if (find_model_ptr(final_name)) {
            throw GraphError("Duplicate block instance name: " + final_name);
        }

        auto wrapper = std::make_shared<BlockWrapper<TBlock>>(final_name, std::forward<Args>(args)...);
        TBlock& block = wrapper->block();
        blocks_.push_back(std::move(wrapper));
        return block;
    }

    BlockModel& add_block(std::unique_ptr<BlockModel> block_model) {
        return add_block(std::shared_ptr<BlockModel>(std::move(block_model)));
    }

    BlockModel& add_block(std::shared_ptr<BlockModel> block_model) {
        if (finalized_) {
            throw GraphError("Cannot add block after graph connections have been finalized");
        }
        if (!block_model) {
            throw GraphError("Cannot add null block model");
        }

        const std::string final_name(block_model->instance_name());
        if (final_name.empty()) {
            throw GraphError("Block instance name must not be empty");
        }
        if (find_model_ptr(final_name)) {
            throw GraphError("Duplicate block instance name: " + final_name);
        }

        blocks_.push_back(std::move(block_model));
        return *blocks_.back();
    }

    const std::vector<std::shared_ptr<BlockModel>>& blocks() const noexcept {
        return blocks_;
    }

    const std::deque<Edge>& edges() const noexcept {
        return edges_;
    }

    BlockModel& block(std::string_view instance_name) {
        auto model = find_model_ptr(instance_name);
        if (!model) {
            throw GraphError("Block instance not found: " + std::string(instance_name));
        }
        return *model;
    }

    template <typename TBlock>
    BlockModel& model_for(TBlock& block) {
        for (const auto& model : blocks_) {
            if (model->raw_block() == static_cast<void*>(&block)) {
                return *model;
            }
        }
        throw GraphError("Block instance is not owned by this graph");
    }

    Edge& connect(BlockModel& source,
                  std::string_view source_port_name,
                  BlockModel& destination,
                  std::string_view destination_port_name,
                  EdgeOptions options = {}) {
        if (finalized_) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "graph connections have already been finalized",
                             EdgeState::ErrorConnecting);
        }
        if (options.capacity == 0) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "buffer capacity must be greater than zero",
                             EdgeState::ErrorConnecting);
        }

        auto source_model = find_model_ptr(source);
        auto destination_model = find_model_ptr(destination);
        if (!source_model || !destination_model) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "both blocks must be owned by this graph",
                             EdgeState::ErrorConnecting);
        }

        PortBase* source_port = source.output_port(source_port_name);
        if (!source_port) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "source output port not found",
                             EdgeState::PortNotFound);
        }

        PortBase* destination_port = destination.input_port(destination_port_name);
        if (!destination_port) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "destination input port not found",
                             EdgeState::PortNotFound);
        }

        const DynamicPort source_dynamic = source_port->dynamic_port();
        const DynamicPort destination_dynamic = destination_port->dynamic_port();
        if (source_dynamic.direction() != PortDirection::OUTPUT ||
            destination_dynamic.direction() != PortDirection::INPUT) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "port direction mismatch",
                             EdgeState::DirectionMismatch);
        }
        if (source_dynamic.port_type() != PortType::STREAM ||
            destination_dynamic.port_type() != PortType::STREAM) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "only stream ports are supported in Stage 3",
                             EdgeState::DirectionMismatch);
        }
        if (source_dynamic.type_key() != destination_dynamic.type_key() ||
            source_dynamic.item_size() != destination_dynamic.item_size() ||
            source_dynamic.item_alignment() != destination_dynamic.item_alignment()) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "port sample types do not match",
                             EdgeState::TypeMismatch);
        }

        if (has_exact_edge(source_model.get(), source_port_name,
                           destination_model.get(), destination_port_name)) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "duplicate edge",
                             EdgeState::DuplicateEdge);
        }
        if (has_destination_edge(destination_model.get(), destination_port_name)) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "destination input port already has a pending connection",
                             EdgeState::DuplicateEdge);
        }
        if (would_create_cycle(source_model.get(), destination_model.get())) {
            throw edge_error(source, source_port_name, destination, destination_port_name,
                             "self-loop or zero-delay cycle rejected",
                             EdgeState::CycleRejected);
        }

        Edge edge;
        edge.source = std::move(source_model);
        edge.destination = std::move(destination_model);
        edge.source_port = std::string(source_port_name);
        edge.destination_port = std::string(destination_port_name);
        edge.requested_capacity = options.capacity;
        edge.actual_capacity = 0;
        edge.state = EdgeState::WaitingToConnect;
        edge.name = std::move(options.name);
        edges_.push_back(std::move(edge));
        return edges_.back();
    }

    template <typename TSource, typename TDestination>
    Edge& connect(TSource& source,
                  std::string_view source_port_name,
                  TDestination& destination,
                  std::string_view destination_port_name,
                  EdgeOptions options = {}) {
        return connect(model_for(source), source_port_name,
                       model_for(destination), destination_port_name,
                       std::move(options));
    }

    bool connect_pending_edges() {
        if (finalized_) {
            return true;
        }
        for (const auto& edge : edges_) {
            if (edge.state != EdgeState::WaitingToConnect &&
                edge.state != EdgeState::Connected) {
                return false;
            }
        }

        std::map<std::pair<BlockModel*, std::string>, std::size_t> capacity_by_source;
        for (const auto& edge : edges_) {
            if (edge.state == EdgeState::WaitingToConnect) {
                auto key = std::make_pair(edge.source.get(), edge.source_port);
                auto& capacity = capacity_by_source[key];
                capacity = std::max(capacity, edge.requested_capacity);
            }
        }

        for (auto& edge : edges_) {
            if (edge.state != EdgeState::WaitingToConnect) {
                continue;
            }

            PortBase* source_port = edge.source->output_port(edge.source_port);
            PortBase* destination_port = edge.destination->input_port(edge.destination_port);
            if (!source_port || !destination_port) {
                edge.state = EdgeState::PortNotFound;
                edge.error_message = format_edge_context(*edge.source, edge.source_port,
                                                         *edge.destination, edge.destination_port) +
                                     ": port disappeared before connection";
                return false;
            }

            const auto key = std::make_pair(edge.source.get(), edge.source_port);
            const std::size_t capacity = capacity_by_source[key];
            const DynamicPort source_dynamic = source_port->dynamic_port();
            const DynamicPort destination_dynamic = destination_port->dynamic_port();
            if (destination_dynamic.connected()) {
                edge.state = EdgeState::ErrorConnecting;
                edge.error_message = format_edge_context(*edge.source, edge.source_port,
                                                         *edge.destination, edge.destination_port) +
                                     ": destination input port is already connected";
                return false;
            }
            if (source_dynamic.connected() &&
                source_port->buffer_size() != 0 &&
                source_port->buffer_size() != detail::next_power_of_two(capacity)) {
                edge.state = EdgeState::ErrorConnecting;
                edge.error_message = format_edge_context(*edge.source, edge.source_port,
                                                         *edge.destination, edge.destination_port) +
                                     ": source output port is already connected with a different buffer capacity";
                return false;
            }
        }

        std::vector<ConnectedEdge> connected_edges;
        std::vector<SourceRollback> source_rollbacks;
        for (auto& edge : edges_) {
            if (edge.state != EdgeState::WaitingToConnect) {
                continue;
            }

            PortBase* source_port = edge.source->output_port(edge.source_port);
            PortBase* destination_port = edge.destination->input_port(edge.destination_port);
            if (!source_port || !destination_port) {
                edge.state = EdgeState::PortNotFound;
                edge.error_message = format_edge_context(*edge.source, edge.source_port,
                                                         *edge.destination, edge.destination_port) +
                                     ": port disappeared before connection";
                return false;
            }

            const auto key = std::make_pair(edge.source.get(), edge.source_port);
            const std::size_t capacity = capacity_by_source[key];
            const bool source_was_connected = source_port->dynamic_port().connected();
            try {
                source_port->connect_to(*destination_port, capacity);
                edge.actual_capacity = source_port->buffer_size();
                edge.state = EdgeState::Connected;
                connected_edges.push_back(ConnectedEdge{&edge, destination_port});
                remember_source_rollback(source_rollbacks, source_port, source_was_connected);
            } catch (const std::exception& ex) {
                rollback_pending_connections(connected_edges, source_rollbacks);
                edge.state = EdgeState::ErrorConnecting;
                edge.error_message = format_edge_context(*edge.source, edge.source_port,
                                                         *edge.destination, edge.destination_port) +
                                     ": " + ex.what();
                return false;
            } catch (...) {
                rollback_pending_connections(connected_edges, source_rollbacks);
                edge.state = EdgeState::ErrorConnecting;
                edge.error_message = format_edge_context(*edge.source, edge.source_port,
                                                         *edge.destination, edge.destination_port) +
                                     ": unknown connection error";
                return false;
            }
        }

        finalized_ = true;
        return true;
    }

    void init() {
        ensure_connected();
        for (auto& block_model : blocks_) {
            block_model->init();
        }
    }

    void start() {
        for (auto& block_model : blocks_) {
            block_model->start();
        }
    }

    void stop() {
        for (auto& block_model : blocks_) {
            block_model->stop();
        }
    }

    void pause() {
        for (auto& block_model : blocks_) {
            block_model->pause();
        }
    }

    void resume() {
        for (auto& block_model : blocks_) {
            block_model->resume();
        }
    }

    void work_once() {
        for (auto& block_model : blocks_) {
            block_model->work();
        }
    }

private:
    std::string next_instance_name() {
        for (;;) {
            std::string candidate = "block_" + std::to_string(auto_instance_index_++);
            if (!find_model_ptr(candidate)) {
                return candidate;
            }
        }
    }

    std::shared_ptr<BlockModel> find_model_ptr(BlockModel& model) const {
        for (const auto& candidate : blocks_) {
            if (candidate.get() == &model) {
                return candidate;
            }
        }
        return {};
    }

    std::shared_ptr<BlockModel> find_model_ptr(std::string_view instance_name) const {
        for (const auto& candidate : blocks_) {
            if (candidate->instance_name() == instance_name) {
                return candidate;
            }
        }
        return {};
    }

    bool has_exact_edge(BlockModel* source,
                        std::string_view source_port_name,
                        BlockModel* destination,
                        std::string_view destination_port_name) const {
        for (const auto& edge : edges_) {
            if (edge.source.get() == source &&
                edge.destination.get() == destination &&
                edge.source_port == source_port_name &&
                edge.destination_port == destination_port_name) {
                return true;
            }
        }
        return false;
    }

    bool has_destination_edge(BlockModel* destination, std::string_view destination_port_name) const {
        for (const auto& edge : edges_) {
            if (edge.destination.get() == destination &&
                edge.destination_port == destination_port_name) {
                return true;
            }
        }
        return false;
    }

    bool would_create_cycle(BlockModel* source, BlockModel* destination) const {
        if (source == destination) {
            return true;
        }
        return has_path(destination, source);
    }

    bool has_path(BlockModel* start, BlockModel* target) const {
        std::vector<BlockModel*> stack{start};
        std::unordered_set<BlockModel*> visited;
        while (!stack.empty()) {
            BlockModel* current = stack.back();
            stack.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            if (current == target) {
                return true;
            }
            for (const auto& edge : edges_) {
                if ((edge.state == EdgeState::WaitingToConnect ||
                     edge.state == EdgeState::Connected) &&
                    edge.source.get() == current) {
                    stack.push_back(edge.destination.get());
                }
            }
        }
        return false;
    }

    void ensure_connected() {
        if (connect_pending_edges()) {
            return;
        }
        for (const auto& edge : edges_) {
            if (edge.state != EdgeState::Connected &&
                !edge.error_message.empty()) {
                throw GraphError(edge.error_message);
            }
        }
        throw GraphError("Graph failed to connect pending edges");
    }

    struct ConnectedEdge {
        Edge* edge = nullptr;
        PortBase* destination_port = nullptr;
    };

    struct SourceRollback {
        PortBase* source_port = nullptr;
        bool was_connected = false;
    };

    static void remember_source_rollback(std::vector<SourceRollback>& rollbacks,
                                         PortBase* source_port,
                                         bool was_connected) {
        for (const auto& rollback : rollbacks) {
            if (rollback.source_port == source_port) {
                return;
            }
        }
        rollbacks.push_back(SourceRollback{source_port, was_connected});
    }

    static void rollback_pending_connections(const std::vector<ConnectedEdge>& connected_edges,
                                             const std::vector<SourceRollback>& source_rollbacks) noexcept {
        for (auto it = connected_edges.rbegin(); it != connected_edges.rend(); ++it) {
            try {
                if (it->destination_port) {
                    it->destination_port->disconnect();
                }
                if (it->edge) {
                    it->edge->state = EdgeState::WaitingToConnect;
                    it->edge->actual_capacity = 0;
                    it->edge->error_message.clear();
                }
            } catch (...) {
            }
        }

        for (auto it = source_rollbacks.rbegin(); it != source_rollbacks.rend(); ++it) {
            if (it->was_connected || !it->source_port) {
                continue;
            }
            try {
                it->source_port->disconnect();
            } catch (...) {
            }
        }
    }

    static std::string format_edge_context(const BlockModel& source,
                                           std::string_view source_port_name,
                                           const BlockModel& destination,
                                           std::string_view destination_port_name) {
        std::ostringstream os;
        os << source.instance_name() << "." << source_port_name
           << " -> " << destination.instance_name() << "." << destination_port_name;
        return os.str();
    }

    static GraphError edge_error(const BlockModel& source,
                                 std::string_view source_port_name,
                                 const BlockModel& destination,
                                 std::string_view destination_port_name,
                                 const std::string& reason,
                                 EdgeState) {
        return GraphError(format_edge_context(source, source_port_name,
                                              destination, destination_port_name) +
                          ": " + reason);
    }

    std::vector<std::shared_ptr<BlockModel>> blocks_;
    std::deque<Edge> edges_;
    bool finalized_ = false;
    std::size_t auto_instance_index_ = 0;
};

} // namespace cy::flowgraph

#endif // CYCORE_GRAPH_H
