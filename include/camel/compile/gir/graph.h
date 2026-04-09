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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <unordered_map>

#include "arena.h"
#include "camel/core/data.h"
#include "camel/core/slot.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/debug.h"
#include "camel/utils/exstore.h"
#include "camel/utils/type.h"
#include "types.h"

class OperatorIndex;
class Tuple;

namespace camel::core::context {}

namespace camel::source {
class SourceContext;
}

namespace camel::compile::gir {

class Builder;
class GraphBuilder;

using Type              = camel::core::type::Type;
using FunctionType      = camel::core::type::FunctionType;
using TupleType         = camel::core::type::TupleType;
using data_vec_t        = camel::core::data::data_vec_t;
using data_ptr_t        = camel::core::data::data_ptr_t;
using static_slot_vec_t = std::vector<slot_t>;

struct GraphBuilderState {
    std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> subGraphs;
    std::unordered_set<graph_ptr_t> dependencies;
    node_vec_t normPorts, withPorts, closure;
    node_vec_t nodes;
    static_slot_vec_t staticDataArr = {NullSlot};
    FunctionType *funcType          = nullptr;
    TupleType *staticDataType       = nullptr;
    TupleType *runtimeDataType      = nullptr;
    TupleType *closureType          = nullptr;
    size_t runtimeDataSize          = 1;
    Node *exitNode                  = nullptr;
    bool looped                     = false;
    bool parameterized              = false;
};

enum class SealState {
    Draft,
    Sealing,
    Sealed,
};

// =============================================================================
// Graph: the final read-only product for a GIR function/subgraph.
//
// Once GraphBuilder::sealGraph() seals a Graph, it becomes immutable.
// All structural edits (node add/remove, edge rewiring, layout rearrangement)
// happen in the GraphDraft / GraphBuilder draft state and are exported as a
// sealed graph only once through sealGraph().
//
// After sealing, each node's adjacency vectors are moved to fixed-size arrays
// in the arena (frozen mode), draft vectors are released, and structural edits
// are no longer allowed.
//
// extras_ provides O(1) graph-level cache slots (JIT entry, topo cache, etc.).
// Each backend/tool defines its own index and invalidation protocol; Graph only
// offers the generic getExtra / setExtra interface.
// =============================================================================

class Graph : public std::enable_shared_from_this<Graph> {
  public:
    static std::string makeStableId(const std::string &name);

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
    bool looped() const { return activeState() ? activeState()->looped : looped_; }
    bool empty() const { return (activeState() ? activeState()->nodes : nodes_).empty(); }
    graph_ptr_t outer() const;
    size_t inDegree() const {
        return (activeState() ? activeState()->dependencies : dependencies_).size();
    }
    size_t outDegree() const { return dependents_.size(); }

    std::string toString() const;

    /// Sealed means this graph has passed sealGraph, has complete slot numbering,
    /// layout, and runtime static data, and all node adjacency has been frozen
    /// into FrozenRegion. The state machine is one-way:
    /// Draft -> Sealing -> Sealed.
    ///
    /// Constraints:
    /// - sealed graphs are read-only;
    /// - the mutable draft view exists only in builderState_ (staging);
    /// - Graph's "_" fields are mirrored caches in the draft phase, kept only
    ///   for read-only compatibility.
    bool finalized() const { return sealState_ == SealState::Sealed; }
    bool hasDraftStaging() const { return builderState_ != nullptr; }
    bool hasMutableDraftView() const { return hasDraftStaging() && !finalized(); }

    FunctionType *funcType() const {
        return activeState() ? activeState()->funcType : signature_.funcType;
    }
    graph_arena_ptr_t arena() const { return arena_; }
    size_t frameSize() const { return frameSize_; }
    ::Tuple *staticArea() const { return staticArea_; }
    bool hasFrameLayout() const { return frameSize_ != 0 && staticArea_ != nullptr; }
    const TupleType *staticDataType() const {
        return activeState() ? activeState()->staticDataType : signature_.staticDataType;
    }
    const TupleType *runtimeDataType() const {
        return activeState() ? activeState()->runtimeDataType : signature_.runtimeDataType;
    }
    const TupleType *closureType() const {
        return activeState() ? activeState()->closureType : signature_.closureType;
    }

    const static_slot_vec_t &staticDataArr() const {
        return activeState() ? activeState()->staticDataArr : staticDataArr_;
    }
    slot_t getStaticDataSlot(data_idx_t index) const;
    size_t staticDataSize() const {
        if (hasPackedStaticData_) {
            return packedStaticDataSize_;
        }
        return staticDataArr().size();
    }
    size_t runtimeDataSize() const {
        return activeState() ? activeState()->runtimeDataSize : signature_.runtimeDataSize;
    }

    std::optional<std::unordered_set<graph_ptr_t>>
    getSubGraphsByName(const std::string &name) const;
    const std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> &subGraphs() const {
        return activeState() ? activeState()->subGraphs : subGraphs_;
    }

    const std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> &dependents() const {
        return dependents_;
    }
    const std::unordered_set<graph_ptr_t> &dependencies() const {
        return activeState() ? activeState()->dependencies : dependencies_;
    }
    bool parameterized() const {
        return activeState() ? activeState()->parameterized : parameterized_;
    }

    Node *exitNode() const;
    Node *outputNode() const;

    const node_vec_t &nodes() const { return activeState() ? activeState()->nodes : nodes_; }
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
    const node_vec_t &normPorts() const {
        return activeState() ? activeState()->normPorts : normPorts_;
    }
    const node_vec_t &withPorts() const {
        return activeState() ? activeState()->withPorts : withPorts_;
    }
    const node_vec_t &closure() const { return activeState() ? activeState()->closure : closure_; }
    size_t argsCount() const { return normPorts().size() + withPorts().size() + closure().size(); }

    const std::string &nodeDebugEntityId(const Node *node) const;
    const std::string &nodePortName(const Node *node) const;
    const std::string &nodeAccsKey(const Node *node) const;

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
    // Draft-phase reads go through staging; after sealing, staging is consumed
    // and reads fall back to read-only frozen fields.
    const GraphBuilderState *activeState() const { return builderState_.get(); }
    friend class Node;
    friend class PortNode;
    friend class FuncNode;
    friend class OperNode;
    friend class AccsNode;
    friend class Builder;
    friend class GraphBuilder;

    /// After rearrange and before installing the runtime layout: write the
    /// gnode: entity ID and promote the SourceContext debug mapping.
    void promoteNodeDebugIds(camel::source::SourceContext *sourceContext);
    NodeDebugFingerprint computeNodeDebugFingerprintForNode(Node *node, uint64_t tieBreaker) const;
    /// Placeholder ID for draft nodes (pointer hex); replaced with a
    /// content-addressed entity ID during sealing.
    static void installProvisionalNodeStableId(Graph &graph, const Node *node);
    void packStaticSlotsToFrozen();
    void installFinalFrameLayout();

    std::string name_;
    std::string stableId_;
    graph_wptr_t outer_;
    graph_arena_ptr_t arena_;

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

    // --- Node ownership ---
    // Node objects are constructed in GraphArena; tracked destructors let the
    // arena destroy all node objects together when Graph is destroyed.
    // Graph only records the node set for traversal, freezing, and debug-map maintenance.
    std::vector<Node *> ownedNodes_;

    // --- Centralized node attributes ---
    // stableId and portName are not stored on Node itself; Graph manages them centrally.
    // This keeps Node closer to trivial destruction (no std::string members)
    // and avoids dynamic strings in frozen arena nodes.
    std::unordered_map<const Node *, std::string> nodeStableIds_;
    std::unordered_map<const Node *, std::string> nodePortNames_;
    std::unordered_map<const Node *, std::string> nodeAccsKeys_;

    // --- Centralized non-trivial data ownership ---
    // OperNode holds a raw pointer; the actual shared_ptr ownership is managed by Graph.
    // This keeps Node closer to trivial destruction and paves the way for arena-based allocation.
    std::unordered_map<const ::OperatorIndex *, std::shared_ptr<::OperatorIndex>>
        operIndexRegistry_;
    node_vec_t normPorts_, withPorts_, closure_;
    node_vec_t nodes_;
    Node *exitNode_ = nullptr;

    /// One-way terminal flag. Once sealGraph() sets it to true, it never goes back.
    /// To modify a sealed graph, clone a draft copy first.
    SealState sealState_ = SealState::Draft;

    bool looped_                     = false;
    bool parameterized_              = false;
    uint64_t provisionalDebugIdSeed_ = 0;
    std::shared_ptr<GraphBuilderState> builderState_;

    mutable ExtraStorage<4> extras_;
};

} // namespace camel::compile::gir
