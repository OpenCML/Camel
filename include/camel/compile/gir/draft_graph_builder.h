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
 * Created: Apr. 12, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Compile-time host around the shared GraphDraft editing core.
 *
 * This object owns compile-only services that should not live on GraphDraft:
 * - transient allocation used when converting compile-time constants into slots
 * - node cold metadata keyed by stable draft node ids
 * - compile-time operator registry ownership
 *
 * The draft itself stays focused on graph semantics and runtime encodability.
 */

#pragma once

#include "camel/compile/gir/arena.h"
#include "camel/compile/gir/types.h"
#include "camel/core/data.h"
#include "camel/core/operator.h"
#include "camel/runtime/draft.h"
#include "camel/utils/exstore.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

class OperatorIndex;

namespace camel::compile::gir {

class DraftGraphBuilder {
  public:
    using FuncTarget = graph_ptr_t;
    /// The graphs of a LOOP node: the condition (null for a For loop) and the body.
    struct LoopTarget {
        graph_ptr_t cond;
        graph_ptr_t body;
    };
    using DrefTarget = std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>;

    explicit DraftGraphBuilder(camel::core::type::FunctionType *funcType = nullptr);
    ~DraftGraphBuilder();

    DraftGraphBuilder(const DraftGraphBuilder &)            = delete;
    DraftGraphBuilder &operator=(const DraftGraphBuilder &) = delete;
    DraftGraphBuilder(DraftGraphBuilder &&)                 = delete;
    DraftGraphBuilder &operator=(DraftGraphBuilder &&)      = delete;

    runtime::GraphDraft &draft() const { return *draft_; }
    static DraftGraphBuilder *fromDraft(const runtime::GraphDraft *draft);
    graph_arena_ptr_t arena() const { return arena_; }
    const std::string &name() const { return name_; }
    /// The graph's identity (camel/runtime/graph_id.h), the id of the runtime graph it encodes to.
    runtime::GraphId graphId() const { return draft_->id(); }
    camel::core::type::FunctionType *funcType() const { return draft_->funcType(); }
    camel::core::type::TupleType *closureType() const { return draft_->closureType(); }
    bool hasFrameLayout() const { return draft_->hasFrameLayout(); }
    size_t frameSize() const { return draft_->frameSize(); }
    bool parameterized() const { return parameterized_; }
    void setName(std::string name) { name_ = std::move(name); }
    void setOuterGraph(const graph_ptr_t &graph) { outerGraph_ = graph; }
    const graph_ptr_t &outerGraph() const { return outerGraph_; }
    void setParameterized(bool parameterized) { parameterized_ = parameterized; }
    template <typename T, std::size_t Index> T *getExtra() const { return extras_.get<T, Index>(); }
    template <typename T, std::size_t Index> void setExtra(T *ptr) const {
        extras_.set<T, Index>(ptr);
    }

    void setFuncType(camel::core::type::FunctionType *funcType);
    /// Completes the signature with the result type its body decided: an open signature (the
    /// type of a function the program declared, camel/core/type/composite/func.h) is completed
    /// in place, so every value of the function sees it; any other is replaced by the interned
    /// signature with that result type. Interned types are never modified.
    void setExitType(camel::core::type::Type *exitType);
    /// The names the function captures, in capture order (runtime::GraphDraft::captureNames).
    std::vector<std::string_view> captureNames() const;
    /// Records that the function captures `name` (one entry per capture port, in order).
    void addCaptureName(std::string_view name) { draft_->addCaptureName(name); }
    void setClosureType(camel::core::type::TupleType *closureType);

    runtime::gc_slot_idx_t addStaticSlot(slot_t slot);
    runtime::gc_slot_idx_t addStaticSlot(slot_t slot, camel::core::type::Type *type);
    runtime::gc_slot_idx_t addStaticData(
        const camel::core::data::data_ptr_t &data, camel::core::type::Type *runtimeType = nullptr);
    runtime::gc_slot_idx_t addRuntimeSlot(camel::core::type::Type *type);
    void setStaticSlot(runtime::gc_slot_idx_t index, slot_t slot);
    void setStaticData(runtime::gc_slot_idx_t index, const camel::core::data::data_ptr_t &data);

    runtime::DraftNodeRef addStaticDataNode(
        const camel::core::data::data_ptr_t &data, camel::core::type::Type *runtimeType = nullptr);
    runtime::DraftNodeRef 
    addPortNode(camel::core::type::Type *type, std::string name, bool isWith);
    runtime::DraftNodeRef addCastNode(camel::core::type::Type *type);
    runtime::DraftNodeRef addCopyNode(camel::core::type::Type *type);
    runtime::DraftNodeRef addFillNode(camel::core::type::Type *type, const runtime::GCFillBody &body);
    runtime::DraftNodeRef 
    addFillNode(camel::core::type::Type *type, std::span<const std::byte> payload);
    runtime::DraftNodeRef addAccsNode(camel::core::type::Type *type, uint32_t tupleIndex);
    runtime::DraftNodeRef addAccsNode(camel::core::type::Type *type, std::string key);
    runtime::DraftNodeRef addBrchNode(
        camel::core::type::Type *type, runtime::DraftNodeRef joinNode,
        std::span<const runtime::GCBranchArm> arms, runtime::DraftNodeRef defaultArm = nullptr);
    runtime::DraftNodeRef addJoinNode(
        camel::core::type::Type *type, runtime::DraftNodeRef brchNode, runtime::gc_cnt_t armCount);
    runtime::DraftNodeRef 
    addCallNode(camel::core::type::Type *type, const runtime::GCCallBody &body = {});
    runtime::DraftNodeRef addFuncNode(const FuncTarget &target, runtime::gc_slot_idx_t dataIndex);
    runtime::DraftNodeRef addOperNode(const oper_idx_ptr_t &op, runtime::gc_slot_idx_t dataIndex);
    /// A LOOP node of form `kind` running `target` (camel/runtime/loop.h); its value has type
    /// `type`. The two graphs are made dependencies of this one.
    runtime::DraftNodeRef 
    addLoopNode(camel::core::LoopKind kind, const LoopTarget &target, camel::core::type::Type *type);
    /// Makes the OPER node `node` call `op` instead, an overload with the same parameters and
    /// result type (the node's inputs, outputs and slot are kept).
    void retargetOperNode(runtime::DraftNodeRef node, const oper_idx_ptr_t &op);
    runtime::DraftNodeRef addSyncNode();
    runtime::DraftNodeRef addGateNode(camel::core::type::Type *type);
    runtime::DraftNodeRef addDrefNode(DrefTarget target);
    void link(runtime::DraftEdgeKind kind, runtime::DraftNodeRef from, runtime::DraftNodeRef to);
    bool unlink(runtime::DraftEdgeKind kind, runtime::DraftNodeRef from, runtime::DraftNodeRef to);
    void addDependencyGraph(const graph_ptr_t &graph);
    void eraseDependencyGraph(const graph_ptr_t &graph);
    void addSubGraph(const graph_ptr_t &graph);
    void eraseSubGraph(const graph_ptr_t &graph);
    void addStaticGraphRef(const graph_ptr_t &graph);
    void eraseStaticGraphRef(const graph_ptr_t &graph);
    const std::vector<graph_ptr_t> &dependencyGraphs() const { return dependencyGraphs_; }
    const std::vector<graph_ptr_t> &subGraphs() const { return subGraphs_; }
    const std::vector<graph_ptr_t> &staticGraphRefs() const { return staticGraphRefs_; }

    const std::string &nodePortName(runtime::DraftNodeRef node) const;
    const std::string &nodePortName(runtime::gc_node_ref_t nodeId) const {
        return nodePortName(runtime::DraftNodeRef(draft_.get(), nodeId));
    }
    const FuncTarget &funcTarget(runtime::DraftNodeRef node) const;
    const FuncTarget &funcTarget(runtime::gc_node_ref_t nodeId) const {
        return funcTarget(runtime::DraftNodeRef(draft_.get(), nodeId));
    }
    const LoopTarget &loopTarget(runtime::DraftNodeRef node) const;
    const LoopTarget &loopTarget(runtime::gc_node_ref_t nodeId) const {
        return loopTarget(runtime::DraftNodeRef(draft_.get(), nodeId));
    }
    const DrefTarget &drefTarget(runtime::DraftNodeRef node) const;
    const DrefTarget &drefTarget(runtime::gc_node_ref_t nodeId) const {
        return drefTarget(runtime::DraftNodeRef(draft_.get(), nodeId));
    }
    const oper_idx_ptr_t &operTarget(runtime::DraftNodeRef node) const;
    const oper_idx_ptr_t &operTarget(runtime::gc_node_ref_t nodeId) const {
        return operTarget(runtime::DraftNodeRef(draft_.get(), nodeId));
    }
    void registerNodePortName(runtime::DraftNodeRef node, std::string name);
    void registerNodePortName(runtime::gc_node_ref_t nodeId, std::string name) {
        registerNodePortName(runtime::DraftNodeRef(draft_.get(), nodeId), std::move(name));
    }
    void registerFuncTarget(runtime::DraftNodeRef node, FuncTarget target);
    void registerFuncTarget(runtime::gc_node_ref_t nodeId, FuncTarget target) {
        registerFuncTarget(runtime::DraftNodeRef(draft_.get(), nodeId), std::move(target));
    }
    void registerDrefTarget(runtime::DraftNodeRef node, DrefTarget target);
    void registerDrefTarget(runtime::gc_node_ref_t nodeId, DrefTarget target) {
        registerDrefTarget(runtime::DraftNodeRef(draft_.get(), nodeId), std::move(target));
    }
    void registerOperTarget(runtime::DraftNodeRef node, oper_idx_ptr_t target);
    void registerOperTarget(runtime::gc_node_ref_t nodeId, oper_idx_ptr_t target) {
        registerOperTarget(runtime::DraftNodeRef(draft_.get(), nodeId), std::move(target));
    }
    void eraseNodeColdData(runtime::DraftNodeRef node);
    void eraseNodeColdData(runtime::gc_node_ref_t nodeId) {
        eraseNodeColdData(runtime::DraftNodeRef(draft_.get(), nodeId));
    }

    ::OperatorIndex *registerOperIndex(std::shared_ptr<::OperatorIndex> idx);
    std::shared_ptr<::OperatorIndex> lookupOperIndex(const ::OperatorIndex *raw) const;

  private:
    runtime::DraftNodeRef ref(runtime::gc_node_ref_t id) const {
        return draft_->alive(id) ? runtime::DraftNodeRef(draft_.get(), id) : runtime::DraftNodeRef();
    }
    static slot_t makeStaticSlot(
        const camel::core::data::data_ptr_t &data, camel::core::mm::IAllocator &allocator);

    graph_arena_ptr_t arena_;
    std::unique_ptr<runtime::GraphDraft> draft_;
    std::string name_;
    graph_ptr_t outerGraph_;
    bool parameterized_ = false;

    std::unordered_map<runtime::gc_node_ref_t, std::string> nodePortNames_;
    std::unordered_map<runtime::gc_node_ref_t, FuncTarget> funcTargets_;
    std::unordered_map<runtime::gc_node_ref_t, LoopTarget> loopTargets_;
    std::unordered_map<runtime::gc_node_ref_t, DrefTarget> drefTargets_;
    std::unordered_map<runtime::gc_node_ref_t, oper_idx_ptr_t> operTargets_;
    std::vector<graph_ptr_t> dependencyGraphs_;
    std::vector<graph_ptr_t> subGraphs_;
    std::vector<graph_ptr_t> staticGraphRefs_;
    std::unordered_map<const ::OperatorIndex *, std::shared_ptr<::OperatorIndex>>
        operIndexRegistry_;
    mutable ExtraStorage<4> extras_;
};

} // namespace camel::compile::gir
