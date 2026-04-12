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
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <unordered_map>
#include <unordered_set>

#include "arena.h"
#include "camel/core/data.h"
#include "camel/core/slot.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/utils/debug.h"
#include "camel/utils/exstore.h"
#include "camel/utils/type.h"
#include "types.h"

class OperatorIndex;
class Tuple;

namespace camel::core::context {}

namespace camel::runtime {
class GCGraph;
}

namespace camel::source {
class SourceContext;
}

namespace camel::compile::gir {

class Builder;
struct GraphColdData;
class GraphBuilder;

using Type              = camel::core::type::Type;
using FunctionType      = camel::core::type::FunctionType;
using TupleType         = camel::core::type::TupleType;
using data_vec_t        = camel::core::data::data_vec_t;
using data_ptr_t        = camel::core::data::data_ptr_t;
using static_slot_vec_t = std::vector<slot_t>;

// =============================================================================
// Graph: compile-time mutable graph carrier.
//
// Compilation now builds and mutates Graph directly. Runtime graph materialization
// later derives slot numbering and frame layout from this mutable graph.
//
// extras_ provides O(1) graph-level cache slots (JIT entry, topo cache, etc.).
// Each backend/tool defines its own index and invalidation protocol; Graph only
// offers the generic getExtra / setExtra interface.
// =============================================================================

class Graph : public std::enable_shared_from_this<Graph> {
  public:
    static std::string makeStableId(const std::string &name);
    static graph_ptr_t create(
        FunctionType *funcType, const graph_ptr_t &outer = nullptr, const std::string &name = "");

    Graph(const Graph &other)            = delete;
    Graph &operator=(const Graph &other) = delete;
    Graph(Graph &&other)                 = delete;
    Graph &operator=(Graph &&other)      = delete;

    explicit Graph(
        FunctionType *funcType, const graph_ptr_t &graph = nullptr, const std::string &name = "");
    ~Graph();

    static graph_ptr_t null() { return nullptr; }

    bool operator==(const Graph &other) const { return this == &other; }
    bool operator!=(const Graph &other) const { return !(this == &other); }

    bool isRoot() const { return !outer_.lock(); }
    bool isMacro() const { return signature_.funcType && signature_.funcType->modifiers().macro(); }
    const std::string &name() const { return name_; }
    std::string mangledName() const { return name_ + std::format("<{}>", funcType()->mangle()); }
    const std::string &stableId() const { return stableId_; }
    std::string location() const;
    bool looped() const { return looped_; }
    bool empty() const { return nodes_.empty(); }
    graph_ptr_t outer() const;
    size_t inDegree() const { return dependencies_.size(); }
    size_t outDegree() const { return dependents_.size(); }

    std::string toString() const;

    // Compile-time editable graph operations. Graph is no longer a public
    // cross-stage IR; during compilation it is the mutable carrier itself.
    Node *ownNode(Node *node);
    void bindDraftNode(Node *node, runtime::gc_node_ref_t draftId);
    runtime::gc_node_ref_t draftNodeId(const Node *node) const;
    runtime::gc_node_ref_t tryDraftNodeId(const Node *node) const;
    bool hasDraftNode(const Node *node) const;
    data_idx_t addStaticSlot(slot_t slot);
    data_idx_t addStaticData(const data_ptr_t &data);
    data_idx_t addRuntimeData();
    void setStaticSlot(data_idx_t index, slot_t slot);
    void setStaticData(data_idx_t index, const data_ptr_t &data);
    void addNode(Node *node);
    void eraseNode(Node *node);
    void addPort(Node *node, bool isWith = false);
    void addClosure(Node *node);
    void parametrizeClosure();
    void setOutput(Node *node);
    void addSubGraph(const graph_ptr_t &subGraph);
    void eraseSubGraph(const graph_ptr_t &subGraph);
    void addDependency(const graph_ptr_t &dependency);
    void eraseDependency(const graph_ptr_t &dependency);
    void touch();
    void refreshDerivedLayout();

    FunctionType *funcType() const { return signature_.funcType; }
    graph_arena_ptr_t arena() const;
    GraphBuilder &builder() const;
    size_t frameSize() const { return frameSize_; }
    ::Tuple *staticArea() const { return staticArea_; }
    bool hasFrameLayout() const { return frameSize_ != 0 && staticArea_ != nullptr; }
    const TupleType *staticDataType() const { return signature_.staticDataType; }
    const TupleType *runtimeDataType() const { return signature_.runtimeDataType; }
    const TupleType *closureType() const { return signature_.closureType; }

    const static_slot_vec_t &staticDataArr() const;
    slot_t getStaticDataSlot(data_idx_t index) const;
    size_t staticDataSize() const;
    size_t runtimeDataSize() const { return signature_.runtimeDataSize; }

    std::optional<std::unordered_set<graph_ptr_t>>
    getSubGraphsByName(const std::string &name) const;
    const std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> &subGraphs() const {
        return subGraphs_;
    }

    const std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> &dependents() const {
        return dependents_;
    }
    const std::unordered_set<graph_ptr_t> &dependencies() const { return dependencies_; }
    bool parameterized() const { return parameterized_; }

    bool hasOutput() const { return exitNode_ != nullptr; }
    Node *exitNode() const;
    Node *outputNode() const;

    const node_vec_t &nodes() const { return nodes_; }
    node_vec_t ports() const {
        const auto &norm = normPorts();
        const auto &with = withPorts();
        node_vec_t ports;
        ports.reserve(norm.size() + with.size());
        ports.insert(ports.end(), norm.begin(), norm.end());
        ports.insert(ports.end(), with.begin(), with.end());
        return ports;
    }
    bool hasPorts() const { return !normPorts().empty() || !withPorts().empty(); }
    bool hasClosure() const { return !closure().empty(); }
    const node_vec_t &normPorts() const { return normPorts_; }
    const node_vec_t &withPorts() const { return withPorts_; }
    const node_vec_t &closure() const { return closure_; }
    size_t argsCount() const { return normPorts().size() + withPorts().size() + closure().size(); }

    const std::string &nodeDebugEntityId(const Node *node) const;
    const std::string &nodePortName(const Node *node) const;
    const std::string &nodeAccsKey(const Node *node) const;
    void registerNodePortName(const Node *node, std::string name);
    void registerNodeAccsKey(const Node *node, std::string key);

    /// Register an OperatorIndex shared_ptr in this graph and return the raw
    /// pointer for OperNode to hold.
    ::OperatorIndex *registerOperIndex(std::shared_ptr<::OperatorIndex> idx);
    /// Look up the shared_ptr for a raw pointer in the registry (used during
    /// clone to transfer ownership to the new graph).
    std::shared_ptr<::OperatorIndex> lookupOperIndex(const ::OperatorIndex *raw) const;

    /// Generic extra cache slots. Each backend/tool defines its own index;
    /// Graph does not know the specific purpose.
    template <typename T, std::size_t Index> T *getExtra() const { return extras_.get<T, Index>(); }
    template <typename T, std::size_t Index> void setExtra(T *ptr) const {
        extras_.set<T, Index>(ptr);
    }

  private:
    void markMutated();
    friend class Node;
    friend class PortNode;
    friend class FuncNode;
    friend class OperNode;
    friend class AccsNode;
    friend class Builder;

    /// Legacy draft export helper: after rearrange and before installing the
    /// runtime layout, write the gnode: entity ID and promote SourceContext debug mapping.
    void promoteNodeDebugIds(camel::source::SourceContext *sourceContext);
    NodeDebugFingerprint computeNodeDebugFingerprintForNode(Node *node, uint64_t tieBreaker) const;
    /// Placeholder ID for mutable compile nodes; runtime encoding later
    /// promotes it to a content-addressed entity ID when debug metadata is emitted.
    static void installProvisionalNodeStableId(Graph &graph, const Node *node);
    void setNodeDebugEntityId(const Node *node, std::string id);
    void eraseNodeColdData(const Node *node);
    void packStaticSlotsToFrozen();
    void installFinalFrameLayout();

    std::string name_;
    std::string stableId_;
    graph_wptr_t outer_;
    std::unique_ptr<GraphBuilder> builder_;

    std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> subGraphs_;
    std::unordered_set<graph_ptr_t> dependencies_;
    std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> dependents_;

    struct SignatureLayout {
        FunctionType *funcType     = nullptr;
        TupleType *staticDataType  = nullptr;
        TupleType *runtimeDataType = nullptr;
        TupleType *closureType     = nullptr;
        size_t runtimeDataSize     = 1;
    } signature_;
    static_slot_vec_t staticDataArr_ = {NullSlot};
    bool hasPackedStaticData_        = false;
    slot_t *packedStaticData_        = nullptr;
    size_t packedStaticDataSize_     = 0;
    size_t frameSize_                = 0;
    ::Tuple *staticArea_             = nullptr;

    node_vec_t normPorts_, withPorts_, closure_;
    node_vec_t nodes_;
    Node *exitNode_ = nullptr;

    bool looped_                     = false;
    bool parameterized_              = false;
    uint64_t provisionalDebugIdSeed_ = 0;

    mutable ExtraStorage<4> extras_;
};

} // namespace camel::compile::gir
