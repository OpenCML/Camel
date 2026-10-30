// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include "camel/runtime/graph_txn.h"
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
    using DrefTarget = std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>;

    /// A builder for the graph `transaction` owns: the front end never makes a graph of its own,
    /// it writes in the one it was handed.
    explicit DraftGraphBuilder(std::shared_ptr<runtime::GraphTransaction> transaction);
    ~DraftGraphBuilder();

    DraftGraphBuilder(const DraftGraphBuilder &)            = delete;
    DraftGraphBuilder &operator=(const DraftGraphBuilder &) = delete;
    DraftGraphBuilder(DraftGraphBuilder &&)                 = delete;
    DraftGraphBuilder &operator=(DraftGraphBuilder &&)      = delete;

    runtime::GraphDraft &draft() const { return transaction_->edit(); }
    /// The transaction the graph belongs to: the only handle to the graph being built.
    runtime::GraphTransaction &transaction() const { return *transaction_; }
    static DraftGraphBuilder *fromDraft(const runtime::GraphDraft *draft);
    graph_arena_ptr_t arena() const { return arena_; }
    const std::string &name() const { return name_; }
    /// The graph's identity (camel/runtime/graph_id.h), the id of the runtime graph it encodes to.
    runtime::GraphId graphId() const { return draft().id(); }
    camel::core::type::FunctionType *funcType() const { return draft().funcType(); }
    camel::core::type::TupleType *closureType() const { return draft().closureType(); }
    bool hasFrameLayout() const { return draft().hasFrameLayout(); }
    size_t frameSize() const { return draft().frameSize(); }
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
    /// Completes the signature with the resources its body was measured to reach: what a function
    /// promises about resources is part of its type, so a function that wrote no clause promises
    /// what it was measured to touch, written where the type is. An open signature is completed in
    /// place, so every value of the function and every reader of its type sees the same set; any
    /// other is replaced by the interned signature with those resources. Interned types are never
    /// modified.
    void setResources(const camel::core::sem::ResourceSignature &resources);
    /// The names the function captures, in capture order (runtime::GraphDraft::captureNames).
    std::vector<std::string_view> captureNames() const;
    /// Records that the function captures `name` (one entry per capture port, in order).
    void addCaptureName(std::string_view name) { draft().addCaptureName(name); }
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
        camel::core::type::Type *type, runtime::DraftNodeRef joinNode, runtime::gc_cnt_t armCount);
    runtime::DraftNodeRef addJoinNode(
        camel::core::type::Type *type, runtime::DraftNodeRef brchNode, runtime::gc_cnt_t armCount);
    runtime::DraftNodeRef
    addCallNode(camel::core::type::Type *type, const runtime::GCCallBody &body = {});
    runtime::DraftNodeRef addFuncNode(const FuncTarget &target, runtime::gc_slot_idx_t dataIndex);
    /// A direct call whose value carries the new version of the captured variables the callee
    /// writes: `type` is what the call hands on (Builder::callValueTypeOf) and
    /// `writtenCaptures` the mask of the callee's capture table it packs
    /// (camel::runtime::GCFuncRef::reserved).
    runtime::DraftNodeRef addFuncNode(
        const FuncTarget &target, camel::core::type::Type *type, uint32_t writtenCaptures,
        runtime::gc_slot_idx_t dataIndex = 0);
    runtime::DraftNodeRef addOperNode(const oper_idx_ptr_t &op, runtime::gc_slot_idx_t dataIndex);
    /// A LOOP node of form `kind` (camel/runtime/loop.h) whose condition and body are regions of
    /// this graph; its value has type `type`.
    runtime::DraftNodeRef addRegionLoopNode(camel::core::LoopKind kind, camel::core::type::Type *type);
    /// A Port node that is a port of a region (the state of a loop), not a parameter of the graph.
    runtime::DraftNodeRef addRegionPortNode(camel::core::type::Type *type, std::string name);
    /// Makes the OPER node `node` call `op` instead, an overload with the same parameters and
    /// result type (the node's inputs, outputs and slot are kept).
    void retargetOperNode(runtime::DraftNodeRef node, const oper_idx_ptr_t &op);
    runtime::DraftNodeRef addGateNode(camel::core::type::Type *type);
    runtime::DraftNodeRef addDrefNode(DrefTarget target);
    void link(
        runtime::DraftEdgeKind kind, runtime::DraftNodeRef from, runtime::DraftNodeRef to,
        runtime::PortRole role = runtime::PortRole::Normal);
    bool unlink(
        runtime::DraftEdgeKind kind, runtime::DraftNodeRef from, runtime::DraftNodeRef to,
        runtime::PortRole role = runtime::PortRole::Normal);
    void addDependencyGraph(const graph_ptr_t &graph);
    void eraseDependencyGraph(const graph_ptr_t &graph);
    void addSubGraph(const graph_ptr_t &graph);
    void eraseSubGraph(const graph_ptr_t &graph);
    void addStaticGraphRef(const graph_ptr_t &graph);
    void eraseStaticGraphRef(const graph_ptr_t &graph);
    /// A graph a declaration tag of this graph's declaration names (a derivative rule). It is
    /// kept in the reference table so that it is encoded with the program and can be read back
    /// from the frozen graph.
    void addRuleGraphRef(const graph_ptr_t &graph);
    const std::vector<graph_ptr_t> &dependencyGraphs() const { return dependencyGraphs_; }
    const std::vector<graph_ptr_t> &subGraphs() const { return subGraphs_; }
    const std::vector<graph_ptr_t> &staticGraphRefs() const { return staticGraphRefs_; }
    const std::vector<graph_ptr_t> &ruleGraphs() const { return ruleGraphRefs_; }

    const std::string &nodePortName(runtime::DraftNodeRef node) const;
    const std::string &nodePortName(runtime::gc_node_ref_t nodeId) const {
        return nodePortName(runtime::DraftNodeRef(&draft(), nodeId));
    }
    const FuncTarget &funcTarget(runtime::DraftNodeRef node) const;
    const FuncTarget &funcTarget(runtime::gc_node_ref_t nodeId) const {
        return funcTarget(runtime::DraftNodeRef(&draft(), nodeId));
    }
    const DrefTarget &drefTarget(runtime::DraftNodeRef node) const;
    const DrefTarget &drefTarget(runtime::gc_node_ref_t nodeId) const {
        return drefTarget(runtime::DraftNodeRef(&draft(), nodeId));
    }
    const oper_idx_ptr_t &operTarget(runtime::DraftNodeRef node) const;
    const oper_idx_ptr_t &operTarget(runtime::gc_node_ref_t nodeId) const {
        return operTarget(runtime::DraftNodeRef(&draft(), nodeId));
    }
    void registerNodePortName(runtime::DraftNodeRef node, std::string name);
    void registerNodePortName(runtime::gc_node_ref_t nodeId, std::string name) {
        registerNodePortName(runtime::DraftNodeRef(&draft(), nodeId), std::move(name));
    }
    void registerFuncTarget(runtime::DraftNodeRef node, FuncTarget target);
    void registerFuncTarget(runtime::gc_node_ref_t nodeId, FuncTarget target) {
        registerFuncTarget(runtime::DraftNodeRef(&draft(), nodeId), std::move(target));
    }
    void registerDrefTarget(runtime::DraftNodeRef node, DrefTarget target);
    void registerDrefTarget(runtime::gc_node_ref_t nodeId, DrefTarget target) {
        registerDrefTarget(runtime::DraftNodeRef(&draft(), nodeId), std::move(target));
    }
    void registerOperTarget(runtime::DraftNodeRef node, oper_idx_ptr_t target);
    void registerOperTarget(runtime::gc_node_ref_t nodeId, oper_idx_ptr_t target) {
        registerOperTarget(runtime::DraftNodeRef(&draft(), nodeId), std::move(target));
    }
    void eraseNodeColdData(runtime::DraftNodeRef node);
    void eraseNodeColdData(runtime::gc_node_ref_t nodeId) {
        eraseNodeColdData(runtime::DraftNodeRef(&draft(), nodeId));
    }

    ::OperatorIndex *registerOperIndex(std::shared_ptr<::OperatorIndex> idx);
    std::shared_ptr<::OperatorIndex> lookupOperIndex(const ::OperatorIndex *raw) const;

  private:
    runtime::DraftNodeRef ref(runtime::gc_node_ref_t id) const {
        return draft().alive(id) ? runtime::DraftNodeRef(&draft(), id) : runtime::DraftNodeRef();
    }
    static slot_t makeStaticSlot(
        const camel::core::data::data_ptr_t &data, camel::core::mm::IAllocator &allocator);

    graph_arena_ptr_t arena_;
    /// The graph's owner. The builder holds it but does not own the graph's shape: it writes in
    /// the storage the transaction keeps.
    std::shared_ptr<runtime::GraphTransaction> transaction_;
    std::string name_;
    graph_ptr_t outerGraph_;
    bool parameterized_ = false;

    std::unordered_map<runtime::gc_node_ref_t, std::string> nodePortNames_;
    std::unordered_map<runtime::gc_node_ref_t, FuncTarget> funcTargets_;
    std::unordered_map<runtime::gc_node_ref_t, DrefTarget> drefTargets_;
    std::unordered_map<runtime::gc_node_ref_t, oper_idx_ptr_t> operTargets_;
    std::vector<graph_ptr_t> dependencyGraphs_;
    std::vector<graph_ptr_t> subGraphs_;
    std::vector<graph_ptr_t> staticGraphRefs_;
    std::vector<graph_ptr_t> ruleGraphRefs_;
    std::unordered_map<const ::OperatorIndex *, std::shared_ptr<::OperatorIndex>>
        operIndexRegistry_;
    mutable ExtraStorage<4> extras_;
};

} // namespace camel::compile::gir
