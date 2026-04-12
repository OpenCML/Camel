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
 * Updated: Apr. 12, 2026
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

    DraftGraphBuilder(const DraftGraphBuilder &)            = delete;
    DraftGraphBuilder &operator=(const DraftGraphBuilder &) = delete;
    DraftGraphBuilder(DraftGraphBuilder &&)                 = delete;
    DraftGraphBuilder &operator=(DraftGraphBuilder &&)      = delete;

    runtime::GraphDraft &draft() const { return *draft_; }
    graph_arena_ptr_t arena() const { return arena_; }

    void setFuncType(camel::core::type::FunctionType *funcType);
    void setClosureType(camel::core::type::TupleType *closureType);

    runtime::gc_slot_idx_t addStaticSlot(slot_t slot);
    runtime::gc_slot_idx_t addStaticData(const camel::core::data::data_ptr_t &data);
    runtime::gc_slot_idx_t addRuntimeSlot(camel::core::type::Type *type);
    void setStaticSlot(runtime::gc_slot_idx_t index, slot_t slot);
    void setStaticData(runtime::gc_slot_idx_t index, const camel::core::data::data_ptr_t &data);

    runtime::gc_node_ref_t addStaticDataNode(const camel::core::data::data_ptr_t &data);
    runtime::gc_node_ref_t
    addPortNode(camel::core::type::Type *type, std::string name, bool isWith, bool isVar);
    runtime::gc_node_ref_t addCastNode(camel::core::type::Type *type);
    runtime::gc_node_ref_t addCopyNode(camel::core::type::Type *type);
    runtime::gc_node_ref_t
    addFillNode(camel::core::type::Type *type, const runtime::GCFillBody &body);
    runtime::gc_node_ref_t addAccsNode(camel::core::type::Type *type, uint32_t tupleIndex);
    runtime::gc_node_ref_t addAccsNode(camel::core::type::Type *type, std::string key);
    runtime::gc_node_ref_t addBrchNode(
        camel::core::type::Type *type, runtime::gc_node_ref_t joinRef,
        std::span<const runtime::GCBranchArm> arms,
        runtime::gc_node_ref_t defaultArm = runtime::kInvalidNodeRef);
    runtime::gc_node_ref_t addJoinNode(
        camel::core::type::Type *type, runtime::gc_node_ref_t brchRef, runtime::gc_cnt_t armCount);
    runtime::gc_node_ref_t
    addCallNode(camel::core::type::Type *type, const runtime::GCCallBody &body = {});
    runtime::gc_node_ref_t addBindNode(camel::core::type::Type *type);
    runtime::gc_node_ref_t addFuncNode(const FuncTarget &target, runtime::gc_slot_idx_t dataIndex);
    runtime::gc_node_ref_t addOperNode(const oper_idx_ptr_t &op, runtime::gc_slot_idx_t dataIndex);
    runtime::gc_node_ref_t addSyncNode();
    runtime::gc_node_ref_t addGateNode(camel::core::type::Type *type);
    runtime::gc_node_ref_t addDrefNode(DrefTarget target);
    void link(runtime::DraftEdgeKind kind, runtime::gc_node_ref_t from, runtime::gc_node_ref_t to);
    bool
    unlink(runtime::DraftEdgeKind kind, runtime::gc_node_ref_t from, runtime::gc_node_ref_t to);
    void addDependencyGraph(const graph_ptr_t &graph);
    void eraseDependencyGraph(const graph_ptr_t &graph);
    void addSubGraph(const graph_ptr_t &graph);
    void eraseSubGraph(const graph_ptr_t &graph);
    void addStaticGraphRef(const graph_ptr_t &graph);
    void eraseStaticGraphRef(const graph_ptr_t &graph);
    const std::vector<graph_ptr_t> &dependencyGraphs() const { return dependencyGraphs_; }
    const std::vector<graph_ptr_t> &subGraphs() const { return subGraphs_; }
    const std::vector<graph_ptr_t> &staticGraphRefs() const { return staticGraphRefs_; }

    const std::string &nodeDebugEntityId(runtime::gc_node_ref_t nodeId) const;
    const std::string &nodePortName(runtime::gc_node_ref_t nodeId) const;
    const std::string &nodeAccsKey(runtime::gc_node_ref_t nodeId) const;
    const std::string *tryNodeAccsKey(runtime::gc_node_ref_t nodeId) const;
    bool nodePortIsVar(runtime::gc_node_ref_t nodeId) const;
    const FuncTarget &funcTarget(runtime::gc_node_ref_t nodeId) const;
    const DrefTarget &drefTarget(runtime::gc_node_ref_t nodeId) const;
    const oper_idx_ptr_t &operTarget(runtime::gc_node_ref_t nodeId) const;
    void setNodeDebugEntityId(runtime::gc_node_ref_t nodeId, std::string id);
    void registerNodePortName(runtime::gc_node_ref_t nodeId, std::string name);
    void registerNodeAccsKey(runtime::gc_node_ref_t nodeId, std::string key);
    void registerPortVar(runtime::gc_node_ref_t nodeId, bool isVar);
    void registerFuncTarget(runtime::gc_node_ref_t nodeId, FuncTarget target);
    void registerDrefTarget(runtime::gc_node_ref_t nodeId, DrefTarget target);
    void registerOperTarget(runtime::gc_node_ref_t nodeId, oper_idx_ptr_t target);
    void eraseNodeColdData(runtime::gc_node_ref_t nodeId);

    ::OperatorIndex *registerOperIndex(std::shared_ptr<::OperatorIndex> idx);
    std::shared_ptr<::OperatorIndex> lookupOperIndex(const ::OperatorIndex *raw) const;

  private:
    static slot_t makeStaticSlot(
        const camel::core::data::data_ptr_t &data, camel::core::mm::IAllocator &allocator);

    graph_arena_ptr_t arena_;
    std::unique_ptr<runtime::GraphDraft> draft_;

    std::unordered_map<runtime::gc_node_ref_t, std::string> nodeDebugIds_;
    std::unordered_map<runtime::gc_node_ref_t, std::string> nodePortNames_;
    std::unordered_map<runtime::gc_node_ref_t, std::string> nodeAccsKeys_;
    std::unordered_map<runtime::gc_node_ref_t, bool> nodePortVars_;
    std::unordered_map<runtime::gc_node_ref_t, FuncTarget> funcTargets_;
    std::unordered_map<runtime::gc_node_ref_t, DrefTarget> drefTargets_;
    std::unordered_map<runtime::gc_node_ref_t, oper_idx_ptr_t> operTargets_;
    std::vector<graph_ptr_t> dependencyGraphs_;
    std::vector<graph_ptr_t> subGraphs_;
    std::vector<graph_ptr_t> staticGraphRefs_;
    std::unordered_map<const ::OperatorIndex *, std::shared_ptr<::OperatorIndex>>
        operIndexRegistry_;
};

} // namespace camel::compile::gir
