/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Aug. 13, 2024
 * Updated: Mar. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <unordered_map>

#include "camel/core/data.h"
#include "camel/utils/debug.h"
#include "camel/utils/exstore.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"
#include "types.h"

namespace GraphIR {

// =============================================================================
// Graph：函数/子图，包含节点集合、端口、静态/运行时数据段
// =============================================================================

class Graph : public std::enable_shared_from_this<Graph> {
  public:
    Graph(const Graph &other)            = delete;
    Graph &operator=(const Graph &other) = delete;
    Graph(Graph &&other)                 = delete;
    Graph &operator=(Graph &&other)      = delete;

    explicit Graph(
        FunctionType *funcType, const graph_ptr_t &graph = nullptr, const std::string &name = "")
        : name_(name), outer_(graph), funcType_(funcType), staticDataType_(TupleType::create()),
          runtimeDataType_(TupleType::create()), closureType_(TupleType::create()) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("GIR").debug(
                "Created Graph: {}",
                name_.empty() ? "<anonymous>" : name_));
    }
    ~Graph() {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("GIR").debug(
                "Destroyed Graph: {}",
                name_.empty() ? "<anonymous>" : name_));
    };

    static graph_ptr_t create(
        FunctionType *funcType, const graph_ptr_t &graph = nullptr, const std::string &name = "");

    static graph_ptr_t null() { return nullptr; }

    bool operator==(const Graph &other) const { return this == &other; }
    bool operator!=(const Graph &other) const { return !(this == &other); }

    bool isRoot() const { return !outer_.lock(); }
    const std::string &name() const { return name_; }
    std::string mangledName() const { return name_ + std::format("<{}>", funcType()->mangle()); }
    std::string location() const;
    bool looped() const { return looped_; }
    bool empty() const { return nodes_.empty(); }
    graph_ptr_t outer() const;
    size_t inDegree() const { return dependencies_.size(); }
    size_t outDegree() const { return dependents_.size(); }

    std::string toString() const;

    FunctionType *funcType() const { return funcType_; }
    const TupleType *staticDataType() const { return staticDataType_; }
    const TupleType *runtimeDataType() const { return runtimeDataType_; }
    const TupleType *closureType() const { return closureType_; }

    const data_vec_t &staticDataArr() const { return staticDataArr_; }
    data_idx_t addStaticData(const data_ptr_t &data);
    data_idx_t addRuntimeData();
    void setStaticData(data_idx_t index, const data_ptr_t &data);
    data_ptr_t getStaticData(data_idx_t index) const;
    size_t staticDataSize() const { return staticDataArr_.size(); }
    size_t runtimeDataSize() const { return runtimeDataSize_; }

    std::optional<std::unordered_set<graph_ptr_t>> getSubGraphsByName(const std::string &name);
    void addSubGraph(const graph_ptr_t &graph);
    void delSubGraph(const graph_ptr_t &graph);
    std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> &subGraphs() {
        return subGraphs_;
    }

    std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> &dependents() {
        return dependents_;
    }
    std::unordered_set<graph_ptr_t> &dependencies() { return dependencies_; }
    void addDependency(const graph_ptr_t &graph);
    void delDependency(const graph_ptr_t &graph);

    void addNode(const node_ptr_t &node);
    void delNode(const node_ptr_t &node);

    void addPort(const node_ptr_t &node, bool isWith = false);
    void addClosure(const node_ptr_t &node);
    bool parameterized() const { return parameterized_; }

    void parametrizeClosure();

    const node_ptr_t &exitNode() const;
    const node_ptr_t &outputNode() const;
    bool hasOutput() const { return exitNode_ != nullptr; }
    void setOutput(const node_ptr_t &node);

    const node_vec_t &nodes() { return nodes_; }
    node_vec_t ports() {
        node_vec_t ports;
        ports.insert(ports.end(), normPorts_.begin(), normPorts_.end());
        ports.insert(ports.end(), withPorts_.begin(), withPorts_.end());
        return ports;
    }
    bool hasPorts() const { return !normPorts_.empty() || !withPorts_.empty(); }
    bool hasClosure() const { return !closure_.empty(); }
    const node_vec_t &normPorts() { return normPorts_; }
    const node_vec_t &withPorts() { return withPorts_; }
    const node_vec_t &closure() { return closure_; }
    size_t argsCount() const { return normPorts_.size() + withPorts_.size() + closure_.size(); }

    graph_ptr_t clone() const;

    node_ptr_t inlineNode(const node_ptr_t &node, bool forceSync = false);

    bool dirty() const { return dirty_; }
    void rearrange();

    template <typename T, std::size_t Index> T *getExtra() const { return extras_.get<T, Index>(); }
    template <typename T, std::size_t Index> void setExtra(T *ptr) const {
        extras_.set<T, Index>(ptr);
    }

  private:
    std::string name_;
    graph_wptr_t outer_;

    std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> subGraphs_;
    std::unordered_set<graph_ptr_t> dependencies_;
    std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> dependents_;

    FunctionType *funcType_;
    TupleType *staticDataType_, *runtimeDataType_, *closureType_;
    data_vec_t staticDataArr_ = {nullptr};
    size_t runtimeDataSize_   = 1;

    node_vec_t normPorts_, withPorts_, closure_;
    node_vec_t nodes_;
    node_ptr_t exitNode_;

    bool dirty_ = false;

    bool looped_        = false;
    bool parameterized_ = false;

    mutable ExtraStorage<4> extras_;
};

} // namespace GraphIR
