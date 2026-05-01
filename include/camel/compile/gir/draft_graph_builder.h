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
 * Updated: May. 01, 2026
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

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

class OperatorIndex;

namespace camel::compile::gir {

class DraftGraphBuilder {
  public:
    using FuncTarget = graph_ptr_t;
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
    const std::string &stableId() const { return stableId_; }
    camel::core::type::FunctionType *funcType() const { return draft_->funcType(); }
    camel::core::type::TupleType *runtimeDataType() const { return draft_->runtimeDataType(); }
    camel::core::type::TupleType *closureType() const { return draft_->closureType(); }
    bool hasFrameLayout() const { return draft_->hasFrameLayout(); }
    size_t frameSize() const { return draft_->frameSize(); }
    bool parameterized() const { return parameterized_; }
    bool looped() const { return looped_; }
    void setName(std::string name) { name_ = std::move(name); }
    void setStableId(std::string stableId) { stableId_ = std::move(stableId); }
    void setOuterGraph(const graph_ptr_t &graph) { outerGraph_ = graph; }
    const graph_ptr_t &outerGraph() const { return outerGraph_; }
    void setLooped(bool looped) { looped_ = looped; }
    void setParameterized(bool parameterized) { parameterized_ = parameterized; }
    template <typename T, std::size_t Index> T *getExtra() const { return extras_.get<T, Index>(); }
    template <typename T, std::size_t Index> void setExtra(T *ptr) const {
        extras_.set<T, Index>(ptr);
    }

    void setFuncType(camel::core::type::FunctionType *funcType);
    void setClosureType(camel::core::type::TupleType *closureType);

    runtime::gc_slot_idx_t addStaticSlot(slot_t slot);
    runtime::gc_slot_idx_t addStaticSlot(slot_t slot, camel::core::type::Type *type);
    runtime::gc_slot_idx_t addStaticData(const camel::core::data::data_ptr_t &data);
    runtime::gc_slot_idx_t addRuntimeSlot(camel::core::type::Type *type);
    void setStaticSlot(runtime::gc_slot_idx_t index, slot_t slot);
    void setStaticData(runtime::gc_slot_idx_t index, const camel::core::data::data_ptr_t &data);

    runtime::DraftNode *addStaticDataNode(const camel::core::data::data_ptr_t &data);
    runtime::DraftNode *
    addPortNode(camel::core::type::Type *type, std::string name, bool isWith, bool isVar);
    runtime::DraftNode *addCastNode(camel::core::type::Type *type);
    runtime::DraftNode *addCopyNode(camel::core::type::Type *type);
    runtime::DraftNode *addFillNode(camel::core::type::Type *type, const runtime::GCFillBody &body);
    runtime::DraftNode *addAccsNode(camel::core::type::Type *type, uint32_t tupleIndex);
    runtime::DraftNode *addAccsNode(camel::core::type::Type *type, std::string key);
    runtime::DraftNode *addBrchNode(
        camel::core::type::Type *type, runtime::DraftNode *joinNode,
        std::span<const runtime::GCBranchArm> arms, runtime::DraftNode *defaultArm = nullptr);
    runtime::DraftNode *addJoinNode(
        camel::core::type::Type *type, runtime::DraftNode *brchNode, runtime::gc_cnt_t armCount);
    runtime::DraftNode *
    addCallNode(camel::core::type::Type *type, const runtime::GCCallBody &body = {});
    runtime::DraftNode *addBindNode(camel::core::type::Type *type);
    runtime::DraftNode *addFuncNode(const FuncTarget &target, runtime::gc_slot_idx_t dataIndex);
    runtime::DraftNode *addOperNode(const oper_idx_ptr_t &op, runtime::gc_slot_idx_t dataIndex);
    runtime::DraftNode *addSyncNode();
    runtime::DraftNode *addGateNode(camel::core::type::Type *type);
    runtime::DraftNode *addDrefNode(DrefTarget target);
    void link(runtime::DraftEdgeKind kind, runtime::DraftNode *from, runtime::DraftNode *to);
    bool unlink(runtime::DraftEdgeKind kind, runtime::DraftNode *from, runtime::DraftNode *to);
    void addDependencyGraph(const graph_ptr_t &graph);
    void eraseDependencyGraph(const graph_ptr_t &graph);
    void addSubGraph(const graph_ptr_t &graph);
    void eraseSubGraph(const graph_ptr_t &graph);
    void addStaticGraphRef(const graph_ptr_t &graph);
    void eraseStaticGraphRef(const graph_ptr_t &graph);
    const std::vector<graph_ptr_t> &dependencyGraphs() const { return dependencyGraphs_; }
    const std::vector<graph_ptr_t> &subGraphs() const { return subGraphs_; }
    const std::vector<graph_ptr_t> &staticGraphRefs() const { return staticGraphRefs_; }

    const std::string &nodeDebugEntityId(const runtime::DraftNode *node) const;
    const std::string &nodeDebugEntityId(runtime::gc_node_ref_t nodeId) const {
        return nodeDebugEntityId(draft_->node(nodeId));
    }
    const std::string &nodePortName(const runtime::DraftNode *node) const;
    const std::string &nodePortName(runtime::gc_node_ref_t nodeId) const {
        return nodePortName(draft_->node(nodeId));
    }
    const std::string &nodeAccsKey(const runtime::DraftNode *node) const;
    const std::string &nodeAccsKey(runtime::gc_node_ref_t nodeId) const {
        return nodeAccsKey(draft_->node(nodeId));
    }
    const std::string *tryNodeAccsKey(const runtime::DraftNode *node) const;
    const std::string *tryNodeAccsKey(runtime::gc_node_ref_t nodeId) const {
        return tryNodeAccsKey(draft_->node(nodeId));
    }
    bool nodePortIsVar(const runtime::DraftNode *node) const;
    bool nodePortIsVar(runtime::gc_node_ref_t nodeId) const {
        return nodePortIsVar(draft_->node(nodeId));
    }
    const FuncTarget &funcTarget(const runtime::DraftNode *node) const;
    const FuncTarget &funcTarget(runtime::gc_node_ref_t nodeId) const {
        return funcTarget(draft_->node(nodeId));
    }
    const DrefTarget &drefTarget(const runtime::DraftNode *node) const;
    const DrefTarget &drefTarget(runtime::gc_node_ref_t nodeId) const {
        return drefTarget(draft_->node(nodeId));
    }
    const oper_idx_ptr_t &operTarget(const runtime::DraftNode *node) const;
    const oper_idx_ptr_t &operTarget(runtime::gc_node_ref_t nodeId) const {
        return operTarget(draft_->node(nodeId));
    }
    void setNodeDebugEntityId(const runtime::DraftNode *node, std::string id);
    void setNodeDebugEntityId(runtime::gc_node_ref_t nodeId, std::string id) {
        setNodeDebugEntityId(draft_->node(nodeId), std::move(id));
    }
    void registerNodePortName(const runtime::DraftNode *node, std::string name);
    void registerNodePortName(runtime::gc_node_ref_t nodeId, std::string name) {
        registerNodePortName(draft_->node(nodeId), std::move(name));
    }
    void registerNodeAccsKey(const runtime::DraftNode *node, std::string key);
    void registerNodeAccsKey(runtime::gc_node_ref_t nodeId, std::string key) {
        registerNodeAccsKey(draft_->node(nodeId), std::move(key));
    }
    void registerPortVar(const runtime::DraftNode *node, bool isVar);
    void registerPortVar(runtime::gc_node_ref_t nodeId, bool isVar) {
        registerPortVar(draft_->node(nodeId), isVar);
    }
    void registerFuncTarget(const runtime::DraftNode *node, FuncTarget target);
    void registerFuncTarget(runtime::gc_node_ref_t nodeId, FuncTarget target) {
        registerFuncTarget(draft_->node(nodeId), std::move(target));
    }
    void registerDrefTarget(const runtime::DraftNode *node, DrefTarget target);
    void registerDrefTarget(runtime::gc_node_ref_t nodeId, DrefTarget target) {
        registerDrefTarget(draft_->node(nodeId), std::move(target));
    }
    void registerOperTarget(const runtime::DraftNode *node, oper_idx_ptr_t target);
    void registerOperTarget(runtime::gc_node_ref_t nodeId, oper_idx_ptr_t target) {
        registerOperTarget(draft_->node(nodeId), std::move(target));
    }
    void eraseNodeColdData(const runtime::DraftNode *node);
    void eraseNodeColdData(runtime::gc_node_ref_t nodeId) {
        eraseNodeColdData(draft_->node(nodeId));
    }

    ::OperatorIndex *registerOperIndex(std::shared_ptr<::OperatorIndex> idx);
    std::shared_ptr<::OperatorIndex> lookupOperIndex(const ::OperatorIndex *raw) const;

  private:
    static slot_t makeStaticSlot(
        const camel::core::data::data_ptr_t &data, camel::core::mm::IAllocator &allocator);

    graph_arena_ptr_t arena_;
    std::unique_ptr<runtime::GraphDraft> draft_;
    std::string name_;
    std::string stableId_;
    graph_ptr_t outerGraph_;
    bool looped_        = false;
    bool parameterized_ = false;

    std::unordered_map<const runtime::DraftNode *, std::string> nodeDebugIds_;
    std::unordered_map<const runtime::DraftNode *, std::string> nodePortNames_;
    std::unordered_map<const runtime::DraftNode *, std::string> nodeAccsKeys_;
    std::unordered_map<const runtime::DraftNode *, bool> nodePortVars_;
    std::unordered_map<const runtime::DraftNode *, FuncTarget> funcTargets_;
    std::unordered_map<const runtime::DraftNode *, DrefTarget> drefTargets_;
    std::unordered_map<const runtime::DraftNode *, oper_idx_ptr_t> operTargets_;
    std::vector<graph_ptr_t> dependencyGraphs_;
    std::vector<graph_ptr_t> subGraphs_;
    std::vector<graph_ptr_t> staticGraphRefs_;
    std::unordered_map<const ::OperatorIndex *, std::shared_ptr<::OperatorIndex>>
        operIndexRegistry_;
    mutable ExtraStorage<4> extras_;
};

} // namespace camel::compile::gir
