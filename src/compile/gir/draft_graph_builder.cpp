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
 * Compile-time host around GraphDraft.
 *
 * The old Graph/Node layer should disappear over time. This builder is the
 * destination host for compile-time services that must remain outside the
 * shared editable graph core.
 */

#include "camel/compile/gir/draft_graph_builder.h"

#include "camel/core/rtdata/conv.h"

#include <algorithm>
#include <format>
#include <limits>

namespace camel::compile::gir {

using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;

DraftGraphBuilder::DraftGraphBuilder(camel::core::type::FunctionType *funcType)
    : arena_(std::make_shared<GraphArena>(256 * 1024)),
      draft_(std::make_unique<runtime::GraphDraft>()) {
    draft_->setFuncType(funcType);
}

void DraftGraphBuilder::setFuncType(camel::core::type::FunctionType *funcType) {
    draft_->setFuncType(funcType);
}

void DraftGraphBuilder::setClosureType(camel::core::type::TupleType *closureType) {
    draft_->setClosureType(closureType);
}

slot_t DraftGraphBuilder::makeStaticSlot(
    const camel::core::data::data_ptr_t &data, camel::core::mm::IAllocator &allocator) {
    ASSERT(data != nullptr, "Static data cannot be null.");
    if (data->type()->isGCTraced()) {
        Object *obj = makeGCRefFromGCTracedData(data, allocator);
        return toSlot<Object *>(obj);
    }
    if (data->type()->isPrimitive()) {
        return makeSlotFromPrimitiveData(data);
    }
    ASSERT(
        false,
        std::format(
            "Unsupported static data type '{}' for slot conversion.",
            data->type()->toString()));
    return NullSlot;
}

runtime::gc_slot_idx_t DraftGraphBuilder::addStaticSlot(slot_t slot) {
    const size_t slotIndex = draft_->appendStaticSlot(slot, nullptr);
    ASSERT(
        slotIndex <= static_cast<size_t>(std::numeric_limits<runtime::gc_slot_idx_t>::max()),
        "Compile-time static slot index exceeds runtime gc_slot_idx_t capacity.");
    return static_cast<runtime::gc_slot_idx_t>(-static_cast<int32_t>(slotIndex));
}

runtime::gc_slot_idx_t DraftGraphBuilder::addStaticData(const camel::core::data::data_ptr_t &data) {
    return addStaticSlot(makeStaticSlot(data, arena_->allocator()));
}

runtime::gc_slot_idx_t DraftGraphBuilder::addRuntimeSlot(camel::core::type::Type *type) {
    return draft_->allocateRuntimeSlot(type);
}

void DraftGraphBuilder::setStaticSlot(runtime::gc_slot_idx_t index, slot_t slot) {
    ASSERT(index < 0, "Static slot index must be negative.");
    draft_->setStaticSlot(static_cast<size_t>(-index), slot);
}

void DraftGraphBuilder::setStaticData(
    runtime::gc_slot_idx_t index, const camel::core::data::data_ptr_t &data) {
    setStaticSlot(index, makeStaticSlot(data, arena_->allocator()));
}

runtime::gc_node_ref_t
DraftGraphBuilder::addStaticDataNode(const camel::core::data::data_ptr_t &data) {
    const auto slotIndex = addStaticData(data);
    return draft_->addDataNode(
        data->type(),
        slotIndex,
        static_cast<uint8_t>(runtime::kGCNodeFlagConstant));
}

runtime::gc_node_ref_t DraftGraphBuilder::addPortNode(
    camel::core::type::Type *type, std::string name, bool isWith, bool isVar) {
    const auto slotIndex            = addRuntimeSlot(type);
    const runtime::gc_node_ref_t id = draft_->addPortNode(type, slotIndex);
    registerNodePortName(id, std::move(name));
    registerPortVar(id, isVar);
    if (isWith) {
        draft_->appendWithPort(id);
    } else {
        draft_->appendNormPort(id);
    }
    return id;
}

runtime::gc_node_ref_t DraftGraphBuilder::addCastNode(camel::core::type::Type *type) {
    return draft_->addCastNode(type);
}

runtime::gc_node_ref_t DraftGraphBuilder::addCopyNode(camel::core::type::Type *type) {
    return draft_->addCopyNode(type);
}

runtime::gc_node_ref_t
DraftGraphBuilder::addFillNode(camel::core::type::Type *type, const runtime::GCFillBody &body) {
    return draft_->addFillNode(type, body);
}

runtime::gc_node_ref_t
DraftGraphBuilder::addAccsNode(camel::core::type::Type *type, uint32_t tupleIndex) {
    return draft_->addAccsNode(type, tupleIndex);
}

runtime::gc_node_ref_t
DraftGraphBuilder::addAccsNode(camel::core::type::Type *type, std::string key) {
    const runtime::gc_node_ref_t id = draft_->addAccsNode(type, key);
    registerNodeAccsKey(id, std::move(key));
    return id;
}

runtime::gc_node_ref_t DraftGraphBuilder::addBrchNode(
    camel::core::type::Type *type, runtime::gc_node_ref_t joinRef,
    std::span<const runtime::GCBranchArm> arms, runtime::gc_node_ref_t defaultArm) {
    return draft_->addBrchNode(type, joinRef, arms, defaultArm);
}

runtime::gc_node_ref_t DraftGraphBuilder::addJoinNode(
    camel::core::type::Type *type, runtime::gc_node_ref_t brchRef, runtime::gc_cnt_t armCount) {
    return draft_->addJoinNode(type, brchRef, armCount);
}

runtime::gc_node_ref_t
DraftGraphBuilder::addCallNode(camel::core::type::Type *type, const runtime::GCCallBody &body) {
    return draft_->addCallNode(type, body);
}

runtime::gc_node_ref_t DraftGraphBuilder::addBindNode(camel::core::type::Type *type) {
    return draft_->addBindNode(type);
}

runtime::gc_node_ref_t
DraftGraphBuilder::addFuncNode(const FuncTarget &target, runtime::gc_slot_idx_t dataIndex) {
    ASSERT(target != nullptr, "DraftGraphBuilder function target cannot be null.");
    const runtime::gc_node_ref_t id = draft_->addNode(
        runtime::DraftNodeInit{
            .dataIndex    = dataIndex,
            .dataType     = target->funcType()->exitType(),
            .kind         = runtime::GCNodeKind::Func,
            .runtimeFlags = static_cast<uint8_t>(target->isMacro() ? runtime::kGCNodeFlagMacro : 0),
        });
    registerFuncTarget(id, target);
    return id;
}

runtime::gc_node_ref_t
DraftGraphBuilder::addOperNode(const oper_idx_ptr_t &op, runtime::gc_slot_idx_t dataIndex) {
    ASSERT(op != nullptr, "DraftGraphBuilder operator target cannot be null.");
    const runtime::gc_node_ref_t id = draft_->addNode(
        runtime::DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = op->funcType()->exitType(),
            .kind      = runtime::GCNodeKind::Oper,
        });
    registerOperTarget(id, op);
    return id;
}

runtime::gc_node_ref_t DraftGraphBuilder::addSyncNode() { return draft_->addSyncNode(); }

runtime::gc_node_ref_t DraftGraphBuilder::addGateNode(camel::core::type::Type *type) {
    return draft_->addGateNode(type);
}

runtime::gc_node_ref_t DraftGraphBuilder::addDrefNode(DrefTarget target) {
    const runtime::gc_node_ref_t id = draft_->addDrefNode();
    registerDrefTarget(id, std::move(target));
    return id;
}

void DraftGraphBuilder::link(
    runtime::DraftEdgeKind kind, runtime::gc_node_ref_t from, runtime::gc_node_ref_t to) {
    draft_->appendInput(kind, to, from);
}

bool DraftGraphBuilder::unlink(
    runtime::DraftEdgeKind kind, runtime::gc_node_ref_t from, runtime::gc_node_ref_t to) {
    return draft_->unlinkInput(kind, to, from);
}

void DraftGraphBuilder::addDependencyGraph(const graph_ptr_t &graph) {
    if (graph && std::find(dependencyGraphs_.begin(), dependencyGraphs_.end(), graph) ==
                     dependencyGraphs_.end()) {
        dependencyGraphs_.push_back(graph);
    }
}

void DraftGraphBuilder::eraseDependencyGraph(const graph_ptr_t &graph) {
    if (!graph) {
        return;
    }
    std::erase(dependencyGraphs_, graph);
}

void DraftGraphBuilder::addSubGraph(const graph_ptr_t &graph) {
    if (graph && std::find(subGraphs_.begin(), subGraphs_.end(), graph) == subGraphs_.end()) {
        subGraphs_.push_back(graph);
    }
}

void DraftGraphBuilder::eraseSubGraph(const graph_ptr_t &graph) {
    if (!graph) {
        return;
    }
    std::erase(subGraphs_, graph);
}

void DraftGraphBuilder::addStaticGraphRef(const graph_ptr_t &graph) {
    if (graph && std::find(staticGraphRefs_.begin(), staticGraphRefs_.end(), graph) ==
                     staticGraphRefs_.end()) {
        staticGraphRefs_.push_back(graph);
    }
}

void DraftGraphBuilder::eraseStaticGraphRef(const graph_ptr_t &graph) {
    if (!graph) {
        return;
    }
    std::erase(staticGraphRefs_, graph);
}

const std::string &DraftGraphBuilder::nodeDebugEntityId(runtime::gc_node_ref_t nodeId) const {
    auto it = nodeDebugIds_.find(nodeId);
    ASSERT(it != nodeDebugIds_.end(), "DraftGraphBuilder node debug id not found.");
    return it->second;
}

const std::string &DraftGraphBuilder::nodePortName(runtime::gc_node_ref_t nodeId) const {
    auto it = nodePortNames_.find(nodeId);
    ASSERT(it != nodePortNames_.end(), "DraftGraphBuilder node port name not found.");
    return it->second;
}

const std::string &DraftGraphBuilder::nodeAccsKey(runtime::gc_node_ref_t nodeId) const {
    auto it = nodeAccsKeys_.find(nodeId);
    ASSERT(it != nodeAccsKeys_.end(), "DraftGraphBuilder node ACCS key not found.");
    return it->second;
}

const std::string *DraftGraphBuilder::tryNodeAccsKey(runtime::gc_node_ref_t nodeId) const {
    auto it = nodeAccsKeys_.find(nodeId);
    return it != nodeAccsKeys_.end() ? &it->second : nullptr;
}

bool DraftGraphBuilder::nodePortIsVar(runtime::gc_node_ref_t nodeId) const {
    auto it = nodePortVars_.find(nodeId);
    return it != nodePortVars_.end() ? it->second : false;
}

const DraftGraphBuilder::FuncTarget &
DraftGraphBuilder::funcTarget(runtime::gc_node_ref_t nodeId) const {
    auto it = funcTargets_.find(nodeId);
    ASSERT(it != funcTargets_.end(), "DraftGraphBuilder function target not found.");
    return it->second;
}

const DraftGraphBuilder::DrefTarget &
DraftGraphBuilder::drefTarget(runtime::gc_node_ref_t nodeId) const {
    auto it = drefTargets_.find(nodeId);
    ASSERT(it != drefTargets_.end(), "DraftGraphBuilder DREF target not found.");
    return it->second;
}

const oper_idx_ptr_t &DraftGraphBuilder::operTarget(runtime::gc_node_ref_t nodeId) const {
    auto it = operTargets_.find(nodeId);
    ASSERT(it != operTargets_.end(), "DraftGraphBuilder operator target not found.");
    return it->second;
}

void DraftGraphBuilder::setNodeDebugEntityId(runtime::gc_node_ref_t nodeId, std::string id) {
    nodeDebugIds_[nodeId] = std::move(id);
}

void DraftGraphBuilder::registerNodePortName(runtime::gc_node_ref_t nodeId, std::string name) {
    nodePortNames_[nodeId] = std::move(name);
}

void DraftGraphBuilder::registerNodeAccsKey(runtime::gc_node_ref_t nodeId, std::string key) {
    nodeAccsKeys_[nodeId] = std::move(key);
}

void DraftGraphBuilder::registerPortVar(runtime::gc_node_ref_t nodeId, bool isVar) {
    nodePortVars_[nodeId] = isVar;
}

void DraftGraphBuilder::registerFuncTarget(runtime::gc_node_ref_t nodeId, FuncTarget target) {
    ASSERT(target != nullptr, "DraftGraphBuilder function target cannot be null.");
    funcTargets_[nodeId] = std::move(target);
}

void DraftGraphBuilder::registerDrefTarget(runtime::gc_node_ref_t nodeId, DrefTarget target) {
    drefTargets_[nodeId] = std::move(target);
}

void DraftGraphBuilder::registerOperTarget(runtime::gc_node_ref_t nodeId, oper_idx_ptr_t target) {
    ASSERT(target != nullptr, "DraftGraphBuilder operator target cannot be null.");
    registerOperIndex(target);
    operTargets_[nodeId] = std::move(target);
}

void DraftGraphBuilder::eraseNodeColdData(runtime::gc_node_ref_t nodeId) {
    nodeDebugIds_.erase(nodeId);
    nodePortNames_.erase(nodeId);
    nodeAccsKeys_.erase(nodeId);
    nodePortVars_.erase(nodeId);
    funcTargets_.erase(nodeId);
    drefTargets_.erase(nodeId);
    operTargets_.erase(nodeId);
}

::OperatorIndex *DraftGraphBuilder::registerOperIndex(std::shared_ptr<::OperatorIndex> idx) {
    auto *raw               = idx.get();
    operIndexRegistry_[raw] = std::move(idx);
    return raw;
}

std::shared_ptr<::OperatorIndex>
DraftGraphBuilder::lookupOperIndex(const ::OperatorIndex *raw) const {
    auto it = operIndexRegistry_.find(raw);
    ASSERT(it != operIndexRegistry_.end(), "DraftGraphBuilder operator index not found.");
    return it->second;
}

} // namespace camel::compile::gir
