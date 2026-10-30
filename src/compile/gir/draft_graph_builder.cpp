// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Compile-time host around GraphDraft.
 *
 * The old Graph/Node layer should disappear over time. This builder is the
 * destination host for compile-time services that must remain outside the
 * shared editable graph core.
 */

#include "camel/compile/gir/draft_graph_builder.h"

#include "camel/core/rtdata/conv.h"
#include "camel/core/symbol.h"
#include "camel/core/type/composite/func.h"

#include <algorithm>
#include <format>
#include <limits>
#include <unordered_map>

namespace camel::compile::gir {

using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;

namespace {

// First block of a graph's arena. The arena holds the graph's constant objects and function
// values; a block of 256 KB for each graph that has one filled the 16 MB meta space with 62
// graphs (62 functions that each return a string literal could not be compiled).
constexpr size_t kInitialArenaBlockBytes = 1024;

std::unordered_map<const runtime::GraphDraft *, DraftGraphBuilder *> &draftOwners() {
    static std::unordered_map<const runtime::GraphDraft *, DraftGraphBuilder *> owners;
    return owners;
}

} // namespace

DraftGraphBuilder::DraftGraphBuilder(std::shared_ptr<runtime::GraphTransaction> transaction)
    : arena_(std::make_shared<GraphArena>(kInitialArenaBlockBytes)),
      transaction_(std::move(transaction)) {
    ASSERT(transaction_ != nullptr, "A compile graph is built in a transaction.");
    draftOwners()[&draft()] = this;
}

DraftGraphBuilder::~DraftGraphBuilder() { draftOwners().erase(&draft()); }

DraftGraphBuilder *DraftGraphBuilder::fromDraft(const runtime::GraphDraft *draft) {
    auto it = draftOwners().find(draft);
    return it != draftOwners().end() ? it->second : nullptr;
}

void DraftGraphBuilder::setFuncType(camel::core::type::FunctionType *funcType) {
    draft().setFuncType(funcType);
}

void DraftGraphBuilder::setExitType(camel::core::type::Type *exitType) {
    camel::core::type::FunctionType *funcType = draft().funcType();
    ASSERT(funcType != nullptr, "A graph under construction has a function type.");
    if (auto *open = funcType->asOpen()) {
        open->setExitType(exitType);
    } else {
        draft().setFuncType(funcType->withExitType(exitType));
    }
}

void DraftGraphBuilder::setResources(const camel::core::sem::ResourceSignature &resources) {
    camel::core::type::FunctionType *funcType = draft().funcType();
    ASSERT(funcType != nullptr, "A graph under construction has a function type.");
    if (auto *open = funcType->asOpen()) {
        // The declaration's own signature, which no other function shares: what a function
        // promises about resources is part of its type, so it is written where the type is.
        open->setResources(resources);
    } else {
        draft().setFuncType(funcType->withResources(resources));
    }
}

std::vector<std::string_view> DraftGraphBuilder::captureNames() const {
    std::vector<std::string_view> names;
    names.reserve(draft().captureNames().size());
    for (camel::core::SymId name : draft().captureNames()) {
        names.push_back(camel::core::symbolText(name));
    }
    return names;
}

void DraftGraphBuilder::setClosureType(camel::core::type::TupleType *closureType) {
    draft().setClosureType(closureType);
}

slot_t DraftGraphBuilder::makeStaticSlot(
    const camel::core::data::data_ptr_t &data, camel::core::mm::IAllocator &allocator) {
    ASSERT(data != nullptr, "Static data cannot be null.");
    // A `null` of a nullable type: the constant carries the `T?` it is the null of, and its slot
    // is the null slot of that type whatever the type is. It is not read as a value of the type --
    // `T?` is traced, and there is no traced object behind a null -- so the paths below are not
    // taken for it (camel/core/data/base.h, `isNull`).
    if (data->isNull()) {
        return NullSlot;
    }
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
    const size_t slotIndex = draft().appendStaticSlot(slot, type);
    ASSERT(
        slotIndex <= static_cast<size_t>(std::numeric_limits<runtime::gc_slot_idx_t>::max()),
        "Compile-time static slot index exceeds runtime gc_slot_idx_t capacity.");
    return static_cast<runtime::gc_slot_idx_t>(-static_cast<int32_t>(slotIndex));
}

runtime::gc_slot_idx_t DraftGraphBuilder::addStaticData(
    const camel::core::data::data_ptr_t &data, camel::core::type::Type *runtimeType) {
    return addStaticSlot(
        makeStaticSlot(data, arena_->allocator()),
        runtimeType ? runtimeType : data->type());
}

runtime::gc_slot_idx_t DraftGraphBuilder::addRuntimeSlot(camel::core::type::Type *type) {
    return draft().allocateRuntimeSlot(type);
}

void DraftGraphBuilder::setStaticSlot(runtime::gc_slot_idx_t index, slot_t slot) {
    ASSERT(index < 0, "Static slot index must be negative.");
    draft().setStaticSlot(static_cast<size_t>(-index), slot);
}

void DraftGraphBuilder::setStaticData(
    runtime::gc_slot_idx_t index, const camel::core::data::data_ptr_t &data) {
    setStaticSlot(index, makeStaticSlot(data, arena_->allocator()));
}

runtime::DraftNodeRef DraftGraphBuilder::addStaticDataNode(
    const camel::core::data::data_ptr_t &data, camel::core::type::Type *runtimeType) {
    auto *nodeType       = runtimeType ? runtimeType : data->type();
    const auto slotIndex = addStaticData(data, nodeType);
    const auto id        = draft().addDataNode(
        nodeType,
        slotIndex,
        static_cast<uint8_t>(runtime::kGCNodeFlagConstant));
    return ref(id);
}

runtime::DraftNodeRef DraftGraphBuilder::addPortNode(
    camel::core::type::Type *type, std::string name, bool isWith) {
    const auto slotIndex            = addRuntimeSlot(type);
    const runtime::gc_node_ref_t id = draft().addPortNode(type, slotIndex);
    const auto node                 = ref(id);
    registerNodePortName(node, std::move(name));
    if (isWith) {
        draft().appendWithPort(id);
    } else {
        draft().appendNormPort(id);
    }
    return node;
}

runtime::DraftNodeRef DraftGraphBuilder::addCastNode(camel::core::type::Type *type) {
    return ref(draft().addCastNode(type));
}

runtime::DraftNodeRef DraftGraphBuilder::addCopyNode(camel::core::type::Type *type) {
    return ref(draft().addCopyNode(type));
}

runtime::DraftNodeRef
DraftGraphBuilder::addFillNode(camel::core::type::Type *type, const runtime::GCFillBody &body) {
    return ref(draft().addFillNode(type, body));
}

runtime::DraftNodeRef
DraftGraphBuilder::addFillNode(camel::core::type::Type *type, std::span<const std::byte> payload) {
    return ref(draft().addFillNode(type, payload));
}

runtime::DraftNodeRef
DraftGraphBuilder::addAccsNode(camel::core::type::Type *type, uint32_t tupleIndex) {
    return ref(draft().addAccsNode(type, tupleIndex));
}

runtime::DraftNodeRef DraftGraphBuilder::addAccsNode(camel::core::type::Type *type, std::string key) {
    return ref(draft().addAccsNode(type, key));
}

runtime::DraftNodeRef DraftGraphBuilder::addBrchNode(
    camel::core::type::Type *type, runtime::DraftNodeRef joinNode, runtime::gc_cnt_t armCount) {
    return ref(draft().addBrchNode(type, joinNode.id, armCount));
}

runtime::DraftNodeRef DraftGraphBuilder::addJoinNode(
    camel::core::type::Type *type, runtime::DraftNodeRef brchNode, runtime::gc_cnt_t armCount) {
    return ref(draft().addJoinNode(type, brchNode.id, armCount));
}

runtime::DraftNodeRef
DraftGraphBuilder::addCallNode(camel::core::type::Type *type, const runtime::GCCallBody &body) {
    return ref(draft().addCallNode(type, body));
}

runtime::DraftNodeRef
DraftGraphBuilder::addFuncNode(const FuncTarget &target, runtime::gc_slot_idx_t dataIndex) {
    ASSERT(target != nullptr, "DraftGraphBuilder function target cannot be null.");
    return addFuncNode(target, target->funcType()->exitType(), 0, dataIndex);
}

runtime::DraftNodeRef DraftGraphBuilder::addFuncNode(
    const FuncTarget &target, camel::core::type::Type *type, uint32_t writtenCaptures,
    runtime::gc_slot_idx_t dataIndex) {
    ASSERT(target != nullptr, "DraftGraphBuilder function target cannot be null.");
    const uint8_t runtimeFlags =
        target->draft().isMacroGraph() ? runtime::kGCNodeFlagMacro : 0;
    const runtime::gc_node_ref_t id = draft().addFuncNode(
        runtime::kInvalidGraphId, type, runtimeFlags, dataIndex, writtenCaptures);
    const auto node = ref(id);
    registerFuncTarget(node, target);
    return node;
}

runtime::DraftNodeRef
DraftGraphBuilder::addRegionLoopNode(camel::core::LoopKind kind, camel::core::type::Type *type) {
    return ref(draft().addLoopNode(runtime::GCLoopBody{.kind = kind}, type));
}

runtime::DraftNodeRef
DraftGraphBuilder::addRegionPortNode(camel::core::type::Type *type, std::string name) {
    const auto slotIndex            = addRuntimeSlot(type);
    const runtime::gc_node_ref_t id = draft().addPortNode(type, slotIndex);
    const auto node                 = ref(id);
    if (!name.empty()) {
        registerNodePortName(node, std::move(name));
    }
    return node;
}

runtime::DraftNodeRef
DraftGraphBuilder::addOperNode(const oper_idx_ptr_t &op, runtime::gc_slot_idx_t dataIndex) {
    ASSERT(op != nullptr, "DraftGraphBuilder operator target cannot be null.");
    const runtime::gc_node_ref_t id =
        draft().addOperNode(op->funcType()->exitType(), nullptr, op->uri(), dataIndex);
    const auto node = ref(id);
    if (camel::core::OperatorTable::instance().isMacro(op->uri())) {
        draft().setNodeRuntimeFlags(id, runtime::kGCNodeFlagMacro);
    }
    registerOperTarget(node, op);
    return node;
}

void DraftGraphBuilder::retargetOperNode(runtime::DraftNodeRef node, const oper_idx_ptr_t &op) {
    ASSERT(node && draft().header(node.id)->kind == runtime::GCNodeKind::Oper, "Not an OPER node.");
    ASSERT(op != nullptr, "DraftGraphBuilder operator target cannot be null.");
    auto payload = draft().mutablePayloadOf(node.id);
    ASSERT(payload.size() >= sizeof(runtime::GCOperBody), "OPER payload is too small.");
    auto *body = reinterpret_cast<runtime::GCOperBody *>(payload.data());
    body->op   = nullptr;
    body->oper = camel::core::OperatorTable::instance().intern(op->uri());
    registerOperTarget(node, op);
}

runtime::DraftNodeRef DraftGraphBuilder::addGateNode(camel::core::type::Type *type) {
    return ref(draft().addGateNode(type));
}

runtime::DraftNodeRef DraftGraphBuilder::addDrefNode(DrefTarget target) {
    const runtime::gc_node_ref_t id = draft().addDrefNode();
    const auto node                 = ref(id);
    registerDrefTarget(node, std::move(target));
    return node;
}

void DraftGraphBuilder::link(
    runtime::DraftEdgeKind kind, runtime::DraftNodeRef from, runtime::DraftNodeRef to,
    runtime::PortRole role) {
    draft().appendInput(kind, to.id, from.id, role);
}

bool DraftGraphBuilder::unlink(
    runtime::DraftEdgeKind kind, runtime::DraftNodeRef from, runtime::DraftNodeRef to,
    runtime::PortRole role) {
    return draft().unlinkInput(kind, to.id, from.id, role);
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

void DraftGraphBuilder::addRuleGraphRef(const graph_ptr_t &graph) {
    if (graph && std::find(ruleGraphRefs_.begin(), ruleGraphRefs_.end(), graph) ==
                     ruleGraphRefs_.end()) {
        ruleGraphRefs_.push_back(graph);
        // The draft keeps its own table of graph references: a graph built from it while the
        // passes run (std::macro evaluates a gradient) must name its rule too, not only the
        // program the graphs are encoded into at the end.
        draft().addRuleGraphRef(graph->graphId());
    }
}

const std::string &DraftGraphBuilder::nodePortName(runtime::DraftNodeRef node) const {
    auto it = nodePortNames_.find(node.id);
    ASSERT(it != nodePortNames_.end(), "DraftGraphBuilder node port name not found.");
    return it->second;
}

const DraftGraphBuilder::FuncTarget &
DraftGraphBuilder::funcTarget(runtime::DraftNodeRef node) const {
    auto it = funcTargets_.find(node.id);
    ASSERT(it != funcTargets_.end(), "DraftGraphBuilder function target not found.");
    return it->second;
}

const DraftGraphBuilder::DrefTarget &
DraftGraphBuilder::drefTarget(runtime::DraftNodeRef node) const {
    auto it = drefTargets_.find(node.id);
    ASSERT(it != drefTargets_.end(), "DraftGraphBuilder DREF target not found.");
    return it->second;
}

const oper_idx_ptr_t &DraftGraphBuilder::operTarget(runtime::DraftNodeRef node) const {
    auto it = operTargets_.find(node.id);
    ASSERT(it != operTargets_.end(), "DraftGraphBuilder operator target not found.");
    return it->second;
}

void DraftGraphBuilder::registerNodePortName(runtime::DraftNodeRef node, std::string name) {
    nodePortNames_[node.id] = std::move(name);
}

void DraftGraphBuilder::registerFuncTarget(runtime::DraftNodeRef node, FuncTarget target) {
    ASSERT(target != nullptr, "DraftGraphBuilder function target cannot be null.");
    funcTargets_[node.id] = std::move(target);
}

void DraftGraphBuilder::registerDrefTarget(runtime::DraftNodeRef node, DrefTarget target) {
    drefTargets_[node.id] = std::move(target);
}

void DraftGraphBuilder::registerOperTarget(runtime::DraftNodeRef node, oper_idx_ptr_t target) {
    ASSERT(target != nullptr, "DraftGraphBuilder operator target cannot be null.");
    registerOperIndex(target);
    operTargets_[node.id] = std::move(target);
}

void DraftGraphBuilder::eraseNodeColdData(runtime::DraftNodeRef node) {
    nodePortNames_.erase(node.id);
    funcTargets_.erase(node.id);
    drefTargets_.erase(node.id);
    operTargets_.erase(node.id);
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
