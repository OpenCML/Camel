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
#include <unordered_map>

namespace camel::compile::gir {

using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;

namespace {

std::unordered_map<const runtime::GraphDraft *, DraftGraphBuilder *> &draftOwners() {
    static std::unordered_map<const runtime::GraphDraft *, DraftGraphBuilder *> owners;
    return owners;
}

} // namespace

DraftGraphBuilder::DraftGraphBuilder(camel::core::type::FunctionType *funcType)
    : arena_(std::make_shared<GraphArena>(256 * 1024)),
      draft_(std::make_unique<runtime::GraphDraft>()) {
    draft_->setFuncType(funcType);
    draftOwners()[draft_.get()] = this;
}

DraftGraphBuilder::~DraftGraphBuilder() { draftOwners().erase(draft_.get()); }

DraftGraphBuilder *DraftGraphBuilder::fromDraft(const runtime::GraphDraft *draft) {
    auto it = draftOwners().find(draft);
    return it != draftOwners().end() ? it->second : nullptr;
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
    return addStaticSlot(slot, nullptr);
}

runtime::gc_slot_idx_t
DraftGraphBuilder::addStaticSlot(slot_t slot, camel::core::type::Type *type) {
    const size_t slotIndex = draft_->appendStaticSlot(slot, type);
    ASSERT(
        slotIndex <= static_cast<size_t>(std::numeric_limits<runtime::gc_slot_idx_t>::max()),
        "Compile-time static slot index exceeds runtime gc_slot_idx_t capacity.");
    return static_cast<runtime::gc_slot_idx_t>(-static_cast<int32_t>(slotIndex));
}

runtime::gc_slot_idx_t DraftGraphBuilder::addStaticData(const camel::core::data::data_ptr_t &data) {
    return addStaticSlot(makeStaticSlot(data, arena_->allocator()), data->type());
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

runtime::DraftNode *
DraftGraphBuilder::addStaticDataNode(const camel::core::data::data_ptr_t &data) {
    const auto slotIndex = addStaticData(data);
    const auto id        = draft_->addDataNode(
        data->type(),
        slotIndex,
        static_cast<uint8_t>(runtime::kGCNodeFlagConstant));
    return draft_->node(id);
}

runtime::DraftNode *DraftGraphBuilder::addPortNode(
    camel::core::type::Type *type, std::string name, bool isWith, bool isVar) {
    const auto slotIndex            = addRuntimeSlot(type);
    const runtime::gc_node_ref_t id = draft_->addPortNode(type, slotIndex);
    auto *node                      = draft_->node(id);
    registerNodePortName(node, std::move(name));
    registerPortVar(node, isVar);
    if (isWith) {
        draft_->appendWithPort(id);
    } else {
        draft_->appendNormPort(id);
    }
    return node;
}

runtime::DraftNode *DraftGraphBuilder::addCastNode(camel::core::type::Type *type) {
    return draft_->node(draft_->addCastNode(type));
}

runtime::DraftNode *DraftGraphBuilder::addCopyNode(camel::core::type::Type *type) {
    return draft_->node(draft_->addCopyNode(type));
}

runtime::DraftNode *
DraftGraphBuilder::addFillNode(camel::core::type::Type *type, const runtime::GCFillBody &body) {
    return draft_->node(draft_->addFillNode(type, body));
}

runtime::DraftNode *
DraftGraphBuilder::addAccsNode(camel::core::type::Type *type, uint32_t tupleIndex) {
    return draft_->node(draft_->addAccsNode(type, tupleIndex));
}

runtime::DraftNode *DraftGraphBuilder::addAccsNode(camel::core::type::Type *type, std::string key) {
    const runtime::gc_node_ref_t id = draft_->addAccsNode(type, key);
    auto *node                      = draft_->node(id);
    registerNodeAccsKey(node, std::move(key));
    return node;
}

runtime::DraftNode *DraftGraphBuilder::addBrchNode(
    camel::core::type::Type *type, runtime::DraftNode *joinNode,
    std::span<const runtime::GCBranchArm> arms, runtime::DraftNode *defaultArm) {
    return draft_->node(
        draft_->addBrchNode(type, draft_->nodeId(joinNode), arms, draft_->nodeId(defaultArm)));
}

runtime::DraftNode *DraftGraphBuilder::addJoinNode(
    camel::core::type::Type *type, runtime::DraftNode *brchNode, runtime::gc_cnt_t armCount) {
    return draft_->node(draft_->addJoinNode(type, draft_->nodeId(brchNode), armCount));
}

runtime::DraftNode *
DraftGraphBuilder::addCallNode(camel::core::type::Type *type, const runtime::GCCallBody &body) {
    return draft_->node(draft_->addCallNode(type, body));
}

runtime::DraftNode *DraftGraphBuilder::addBindNode(camel::core::type::Type *type) {
    return draft_->node(draft_->addBindNode(type));
}

runtime::DraftNode *
DraftGraphBuilder::addFuncNode(const FuncTarget &target, runtime::gc_slot_idx_t dataIndex) {
    ASSERT(target != nullptr, "DraftGraphBuilder function target cannot be null.");
    const uint8_t runtimeFlags = static_cast<uint8_t>(
        (target->funcType() && target->funcType()->modifiers().macro()) ? runtime::kGCNodeFlagMacro
                                                                        : 0);
    const runtime::gc_node_ref_t id =
        draft_->addFuncNode(nullptr, target->funcType()->exitType(), runtimeFlags, dataIndex);
    auto *node = draft_->node(id);
    registerFuncTarget(node, target);
    return node;
}

runtime::DraftNode *
DraftGraphBuilder::addOperNode(const oper_idx_ptr_t &op, runtime::gc_slot_idx_t dataIndex) {
    ASSERT(op != nullptr, "DraftGraphBuilder operator target cannot be null.");
    const runtime::gc_node_ref_t id =
        draft_->addOperNode(op->funcType()->exitType(), nullptr, op->uri(), dataIndex);
    auto *node = draft_->node(id);
    registerOperTarget(node, op);
    return node;
}

runtime::DraftNode *DraftGraphBuilder::addSyncNode() { return draft_->node(draft_->addSyncNode()); }

runtime::DraftNode *DraftGraphBuilder::addGateNode(camel::core::type::Type *type) {
    return draft_->node(draft_->addGateNode(type));
}

runtime::DraftNode *DraftGraphBuilder::addDrefNode(DrefTarget target) {
    const runtime::gc_node_ref_t id = draft_->addDrefNode();
    auto *node                      = draft_->node(id);
    registerDrefTarget(node, std::move(target));
    return node;
}

void DraftGraphBuilder::link(
    runtime::DraftEdgeKind kind, runtime::DraftNode *from, runtime::DraftNode *to) {
    draft_->appendInput(kind, draft_->nodeId(to), draft_->nodeId(from));
}

bool DraftGraphBuilder::unlink(
    runtime::DraftEdgeKind kind, runtime::DraftNode *from, runtime::DraftNode *to) {
    return draft_->unlinkInput(kind, draft_->nodeId(to), draft_->nodeId(from));
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

const std::string &DraftGraphBuilder::nodeDebugEntityId(const runtime::DraftNode *node) const {
    auto it = nodeDebugIds_.find(node);
    ASSERT(it != nodeDebugIds_.end(), "DraftGraphBuilder node debug id not found.");
    return it->second;
}

const std::string &DraftGraphBuilder::nodePortName(const runtime::DraftNode *node) const {
    auto it = nodePortNames_.find(node);
    ASSERT(it != nodePortNames_.end(), "DraftGraphBuilder node port name not found.");
    return it->second;
}

const std::string &DraftGraphBuilder::nodeAccsKey(const runtime::DraftNode *node) const {
    auto it = nodeAccsKeys_.find(node);
    ASSERT(it != nodeAccsKeys_.end(), "DraftGraphBuilder node ACCS key not found.");
    return it->second;
}

const std::string *DraftGraphBuilder::tryNodeAccsKey(const runtime::DraftNode *node) const {
    auto it = nodeAccsKeys_.find(node);
    return it != nodeAccsKeys_.end() ? &it->second : nullptr;
}

bool DraftGraphBuilder::nodePortIsVar(const runtime::DraftNode *node) const {
    auto it = nodePortVars_.find(node);
    return it != nodePortVars_.end() ? it->second : false;
}

const DraftGraphBuilder::FuncTarget &
DraftGraphBuilder::funcTarget(const runtime::DraftNode *node) const {
    auto it = funcTargets_.find(node);
    ASSERT(it != funcTargets_.end(), "DraftGraphBuilder function target not found.");
    return it->second;
}

const DraftGraphBuilder::DrefTarget &
DraftGraphBuilder::drefTarget(const runtime::DraftNode *node) const {
    auto it = drefTargets_.find(node);
    ASSERT(it != drefTargets_.end(), "DraftGraphBuilder DREF target not found.");
    return it->second;
}

const oper_idx_ptr_t &DraftGraphBuilder::operTarget(const runtime::DraftNode *node) const {
    auto it = operTargets_.find(node);
    ASSERT(it != operTargets_.end(), "DraftGraphBuilder operator target not found.");
    return it->second;
}

void DraftGraphBuilder::setNodeDebugEntityId(const runtime::DraftNode *node, std::string id) {
    nodeDebugIds_[node] = std::move(id);
}

void DraftGraphBuilder::registerNodePortName(const runtime::DraftNode *node, std::string name) {
    nodePortNames_[node] = std::move(name);
}

void DraftGraphBuilder::registerNodeAccsKey(const runtime::DraftNode *node, std::string key) {
    nodeAccsKeys_[node] = std::move(key);
}

void DraftGraphBuilder::registerPortVar(const runtime::DraftNode *node, bool isVar) {
    nodePortVars_[node] = isVar;
}

void DraftGraphBuilder::registerFuncTarget(const runtime::DraftNode *node, FuncTarget target) {
    ASSERT(target != nullptr, "DraftGraphBuilder function target cannot be null.");
    funcTargets_[node] = std::move(target);
}

void DraftGraphBuilder::registerDrefTarget(const runtime::DraftNode *node, DrefTarget target) {
    drefTargets_[node] = std::move(target);
}

void DraftGraphBuilder::registerOperTarget(const runtime::DraftNode *node, oper_idx_ptr_t target) {
    ASSERT(target != nullptr, "DraftGraphBuilder operator target cannot be null.");
    registerOperIndex(target);
    operTargets_[node] = std::move(target);
}

void DraftGraphBuilder::eraseNodeColdData(const runtime::DraftNode *node) {
    nodeDebugIds_.erase(node);
    nodePortNames_.erase(node);
    nodeAccsKeys_.erase(node);
    nodePortVars_.erase(node);
    funcTargets_.erase(node);
    drefTargets_.erase(node);
    operTargets_.erase(node);
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
