/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 08, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Runtime-side source graph editing support.
 *
 * Runtime rewrite passes still rewrite source
 * GIR today, but the session is now
 * scoped strictly as a cold rewrite utility rather than a
 * general execution
 * dependency. This file owns the minimal source-graph editing workflow needed

 * * by runtime passes: clone the runtime-reachable source closure, retarget graph
 * references
 * inside the cloned closure, materialize static values into an owned
 * editable graph, replace
 * nodes, and seal the rewritten root.
 */

#include "camel/runtime/rewrite.h"

#include "camel/compile/gir/nodes.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"

#include <ranges>
#include <unordered_set>

namespace camel::runtime {

using camel::compile::gir::DataNode;
using camel::compile::gir::FuncNode;
using camel::compile::gir::Graph;
using camel::compile::gir::GraphBuilder;
using camel::compile::gir::LinkType;
using camel::compile::gir::Node;
using camel::compile::gir::NodeType;
using camel::compile::gir::PortNode;
using camel::compile::gir::TupleType;
using camel::compile::gir::Type;
using camel::compile::gir::detail::NodeMutation;
using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;
using camel::core::type::ArrayType;
using camel::core::type::StructType;
using camel::core::type::TypeCode;

namespace {

using ObjectVisitSet   = std::unordered_set<const Object *>;
using ObjectCloneCache = std::unordered_map<const Object *, Object *>;

TupleType *currentClosureTupleType(const camel::compile::gir::graph_ptr_t &graph) {
    std::vector<Type *> closureTypes;
    closureTypes.reserve(graph->closure().size());
    for (Node *node : graph->closure()) {
        closureTypes.push_back(node->dataType());
    }
    return TupleType::create(std::move(closureTypes));
}

template <typename ResolveTarget>
bool retargetStaticSlotGraphs(
    Graph *ownerRaw, GraphBuilder &ownerBuilder, slot_t slot, Type *type,
    ResolveTarget &&resolveTarget, ObjectVisitSet &visited) {
    if (!type || !type->isGCTraced() || slot == NullSlot) {
        return false;
    }

    Object *object = fromSlot<Object *>(slot);
    if (!object || !visited.insert(object).second) {
        return false;
    }

    switch (type->code()) {
    case TypeCode::Function: {
        auto *funcObj = fromSlot<::Function *>(slot);
        if (!funcObj || !funcObj->sourceGraph()) {
            return false;
        }
        bool changed = false;
        if (Graph *mapped = resolveTarget(funcObj->sourceGraph());
            mapped && mapped != funcObj->sourceGraph()) {
            funcObj->setSourceGraph(mapped);
            if (ownerRaw != mapped) {
                auto mappedHandle = mapped->shared_from_this();
                if (!ownerRaw->dependencies().contains(mappedHandle)) {
                    ownerBuilder.addDependency(mappedHandle);
                }
            }
            changed = true;
        }
        if (::Tuple *closure = funcObj->tuple()) {
            auto *closureType = const_cast<camel::core::type::TupleType *>(funcObj->tupleType());
            for (size_t i = 0; i < closureType->size(); ++i) {
                if (!camel::core::type::isGCTraced(closureType->codeAt(i))) {
                    continue;
                }
                changed = retargetStaticSlotGraphs(
                              ownerRaw,
                              ownerBuilder,
                              closure->get<slot_t>(i),
                              closureType->typeAt(i),
                              resolveTarget,
                              visited) ||
                          changed;
            }
        }
        return changed;
    }
    case TypeCode::Tuple: {
        auto *tuple     = fromSlot<::Tuple *>(slot);
        auto *tupleType = static_cast<camel::core::type::TupleType *>(type);
        bool changed    = false;
        for (size_t i = 0; i < tupleType->size(); ++i) {
            if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                continue;
            }
            changed = retargetStaticSlotGraphs(
                          ownerRaw,
                          ownerBuilder,
                          tuple->get<slot_t>(i),
                          tupleType->typeAt(i),
                          resolveTarget,
                          visited) ||
                      changed;
        }
        return changed;
    }
    case TypeCode::Array: {
        auto *array     = fromSlot<::Array *>(slot);
        auto *arrayType = static_cast<ArrayType *>(type);
        if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
            return false;
        }
        bool changed = false;
        for (size_t i = 0; i < array->size(); ++i) {
            changed = retargetStaticSlotGraphs(
                          ownerRaw,
                          ownerBuilder,
                          array->get<slot_t>(i),
                          arrayType->elemType(),
                          resolveTarget,
                          visited) ||
                      changed;
        }
        return changed;
    }
    case TypeCode::Struct: {
        auto *st         = fromSlot<::Struct *>(slot);
        auto *structType = static_cast<StructType *>(type);
        bool changed     = false;
        for (size_t i = 0; i < structType->size(); ++i) {
            if (!camel::core::type::isGCTraced(structType->codeAt(i))) {
                continue;
            }
            changed = retargetStaticSlotGraphs(
                          ownerRaw,
                          ownerBuilder,
                          st->get<slot_t>(i),
                          structType->typeAt(i),
                          resolveTarget,
                          visited) ||
                      changed;
        }
        return changed;
    }
    default:
        return false;
    }
}

slot_t canonicalizeStaticSlot(
    RuntimeSourceGraphEditSession &session, const camel::compile::gir::graph_ptr_t &owner,
    slot_t slot, Type *type, ObjectCloneCache &clones);

slot_t canonicalizeTuple(
    RuntimeSourceGraphEditSession &session, const camel::compile::gir::graph_ptr_t &owner,
    ::Tuple *tuple, camel::core::type::TupleType *tupleType, ObjectCloneCache &clones) {
    if (!tuple) {
        return NullSlot;
    }
    if (auto it = clones.find(tuple); it != clones.end()) {
        return toSlot<Object *>(it->second);
    }

    auto *cloned =
        static_cast<::Tuple *>(tuple->clone(owner->arena()->allocator(), tupleType, false));
    clones[tuple] = cloned;
    for (size_t i = 0; i < tupleType->size(); ++i) {
        if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
            continue;
        }
        cloned->set<slot_t>(
            i,
            canonicalizeStaticSlot(
                session,
                owner,
                cloned->get<slot_t>(i),
                tupleType->typeAt(i),
                clones));
    }
    return toSlot<::Tuple *>(cloned);
}

slot_t canonicalizeArray(
    RuntimeSourceGraphEditSession &session, const camel::compile::gir::graph_ptr_t &owner,
    ::Array *array, ArrayType *arrayType, ObjectCloneCache &clones) {
    if (!array) {
        return NullSlot;
    }
    if (auto it = clones.find(array); it != clones.end()) {
        return toSlot<Object *>(it->second);
    }

    auto *cloned =
        static_cast<::Array *>(array->clone(owner->arena()->allocator(), arrayType, false));
    clones[array] = cloned;
    if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
        return toSlot<::Array *>(cloned);
    }
    for (size_t i = 0; i < cloned->size(); ++i) {
        cloned->set<slot_t>(
            i,
            canonicalizeStaticSlot(
                session,
                owner,
                cloned->get<slot_t>(i),
                arrayType->elemType(),
                clones));
    }
    return toSlot<::Array *>(cloned);
}

slot_t canonicalizeStruct(
    RuntimeSourceGraphEditSession &session, const camel::compile::gir::graph_ptr_t &owner,
    ::Struct *st, StructType *structType, ObjectCloneCache &clones) {
    if (!st) {
        return NullSlot;
    }
    if (auto it = clones.find(st); it != clones.end()) {
        return toSlot<Object *>(it->second);
    }

    auto *cloned =
        static_cast<::Struct *>(st->clone(owner->arena()->allocator(), structType, false));
    clones[st] = cloned;
    for (size_t i = 0; i < structType->size(); ++i) {
        if (!camel::core::type::isGCTraced(structType->codeAt(i))) {
            continue;
        }
        cloned->set<slot_t>(
            i,
            canonicalizeStaticSlot(
                session,
                owner,
                cloned->get<slot_t>(i),
                structType->typeAt(i),
                clones));
    }
    return toSlot<::Struct *>(cloned);
}

slot_t canonicalizeFunction(
    RuntimeSourceGraphEditSession &session, const camel::compile::gir::graph_ptr_t &owner,
    ::Function *func, ObjectCloneCache &clones) {
    if (!func) {
        return NullSlot;
    }
    if (auto it = clones.find(func); it != clones.end()) {
        return toSlot<Object *>(it->second);
    }

    Graph *sourceGraph = func->sourceGraph();
    ASSERT(sourceGraph != nullptr, "Function static value must reference a graph.");
    auto sourceHandle   = sourceGraph->shared_from_this();
    auto canonicalGraph = session.canonicalGraph(sourceHandle);
    ASSERT(canonicalGraph != nullptr, "Runtime source edit session lost canonical function graph.");
    auto *cloned = ::Function::create(
        canonicalGraph.get(),
        currentClosureTupleType(canonicalGraph),
        owner->arena()->allocator());
    clones[func] = cloned;
    if (owner.get() != canonicalGraph.get() && !owner->dependencies().contains(canonicalGraph)) {
        GraphBuilder(owner).addDependency(canonicalGraph);
    }

    if (::Tuple *sourceClosure = func->tuple()) {
        ::Tuple *closure  = cloned->tuple();
        auto *closureType = const_cast<camel::core::type::TupleType *>(cloned->tupleType());
        for (size_t i = 0; i < closureType->size(); ++i) {
            slot_t slotValue = sourceClosure->get<slot_t>(i);
            if (camel::core::type::isGCTraced(closureType->codeAt(i))) {
                slotValue = canonicalizeStaticSlot(
                    session,
                    owner,
                    slotValue,
                    closureType->typeAt(i),
                    clones);
            }
            closure->set<slot_t>(i, slotValue);
        }
    }

    return toSlot<::Function *>(cloned);
}

slot_t canonicalizeStaticSlot(
    RuntimeSourceGraphEditSession &session, const camel::compile::gir::graph_ptr_t &owner,
    slot_t slot, Type *type, ObjectCloneCache &clones) {
    if (!type || !type->isGCTraced() || slot == NullSlot) {
        return slot;
    }

    switch (type->code()) {
    case TypeCode::Function:
        return canonicalizeFunction(session, owner, fromSlot<::Function *>(slot), clones);
    case TypeCode::Tuple:
        return canonicalizeTuple(
            session,
            owner,
            fromSlot<::Tuple *>(slot),
            static_cast<camel::core::type::TupleType *>(type),
            clones);
    case TypeCode::Array:
        return canonicalizeArray(
            session,
            owner,
            fromSlot<::Array *>(slot),
            static_cast<ArrayType *>(type),
            clones);
    case TypeCode::Struct:
        return canonicalizeStruct(
            session,
            owner,
            fromSlot<::Struct *>(slot),
            static_cast<StructType *>(type),
            clones);
    default:
        return slot;
    }
}

std::vector<Node *> collectUseCarrierNodes(const camel::compile::gir::graph_ptr_t &graph) {
    std::vector<Node *> carriers;
    if (!graph) {
        return carriers;
    }
    carriers.reserve(graph->nodes().size() + 1);
    for (Node *node : graph->nodes()) {
        carriers.push_back(node);
    }
    if (Node *exitAnchor = graph->exitNode();
        exitAnchor && std::ranges::find(carriers, exitAnchor) == carriers.end()) {
        carriers.push_back(exitAnchor);
    }
    return carriers;
}

} // namespace

RuntimeSourceGraphEditSession::RuntimeSourceGraphEditSession(
    const camel::compile::gir::graph_ptr_t &sourceRoot,
    const std::vector<camel::compile::gir::graph_ptr_t> &reachableSourceGraphs)
    : sourceRoot_(sourceRoot), reachableSourceGraphs_(reachableSourceGraphs) {
    ASSERT(sourceRoot_ != nullptr, "Runtime source edit session requires a non-null source root.");
    cloneReachableClosure(reachableSourceGraphs_);
    retargetClonedClosure();
}

std::vector<camel::compile::gir::graph_ptr_t>
RuntimeGraphRewriteSession::collectReachableSourceGraphs() const {
    std::vector<camel::compile::gir::graph_ptr_t> graphs;
    graphs.reserve(16);
    for (GCGraph *runtimeGraph : collectReachableRuntimeGraphs()) {
        if (!runtimeGraph) {
            continue;
        }
        const GIR::graph_ptr_t &sourceGraph = runtimeGraph->sourceGraph();
        if (sourceGraph) {
            graphs.push_back(sourceGraph);
        }
    }
    return graphs;
}

RuntimeSourceGraphEditSession RuntimeGraphRewriteSession::createSourceEditSession() const {
    return RuntimeSourceGraphEditSession(sourceRoot_, collectReachableSourceGraphs());
}

camel::compile::gir::graph_ptr_t
RuntimeSourceGraphEditSession::canonicalGraph(const camel::compile::gir::graph_ptr_t &graph) const {
    if (!graph) {
        return nullptr;
    }
    if (auto it = clonedBySource_.find(graph.get()); it != clonedBySource_.end()) {
        return it->second;
    }
    return graph;
}

Node *RuntimeSourceGraphEditSession::canonicalNode(const Node *node) const {
    if (!node) {
        return nullptr;
    }
    if (auto it = clonedNodeBySource_.find(node); it != clonedNodeBySource_.end()) {
        return it->second;
    }
    return const_cast<Node *>(node);
}

bool RuntimeSourceGraphEditSession::ownsGraph(const Graph *graph) const {
    return graph != nullptr && ownedGraphs_.contains(const_cast<Graph *>(graph));
}

std::vector<camel::compile::gir::graph_ptr_t>
RuntimeSourceGraphEditSession::editableGraphs() const {
    std::vector<camel::compile::gir::graph_ptr_t> editable;
    editable.reserve(reachableSourceGraphs_.size());
    for (const auto &sourceGraph : reachableSourceGraphs_) {
        editable.push_back(canonicalGraph(sourceGraph));
    }
    return editable;
}

Node *RuntimeSourceGraphEditSession::materializeStaticValue(
    const camel::compile::gir::graph_ptr_t &owner, slot_t slot, Type *type,
    const RuntimeStaticValueMaterializationOptions &options) {
    ASSERT(owner != nullptr, "Cannot materialize static value into null owner graph.");
    ASSERT(type != nullptr, "Cannot materialize static value with null type.");

    ObjectCloneCache clones;
    slot_t canonicalSlot = canonicalizeStaticSlot(*this, owner, slot, type, clones);
    Node *node           = DataNode::createStaticSlot(*owner, type, canonicalSlot);
    if (options.propagateMacro && type->code() == TypeCode::Function) {
        auto *funcObj = fromSlot<::Function *>(canonicalSlot);
        if (funcObj && funcObj->sourceGraph() && funcObj->sourceGraph()->isMacro()) {
            NodeMutation::setMacro(node, true);
        }
    }
    return node;
}

bool RuntimeSourceGraphEditSession::replaceNode(Node *oldNode, Node *newNode) {
    ASSERT(oldNode != nullptr && newNode != nullptr, "Cannot replace null node.");
    Graph *ownerGraph = &oldNode->graph();
    ASSERT(
        ownerGraph == &newNode->graph(),
        "Runtime source edit replace requires same owner graph.");
    bool changed = NodeMutation::replace(oldNode, newNode);
    GraphBuilder(ownerGraph).touch();
    GraphBuilder(ownerGraph).eraseNode(oldNode);
    changed_ = changed_ || changed;
    return changed;
}

bool RuntimeSourceGraphEditSession::replaceAllUses(Node *oldNode, Node *newNode) {
    ASSERT(oldNode != nullptr && newNode != nullptr, "Cannot replace uses of null node.");
    ASSERT(
        &oldNode->graph() == &newNode->graph(),
        "Runtime source edit replaceUses requires same owner graph.");
    bool changed = NodeMutation::replaceUses(oldNode, newNode);
    GraphBuilder(&oldNode->graph()).touch();
    changed_ = changed_ || changed;
    return changed;
}

void RuntimeSourceGraphEditSession::eraseNode(Node *node) {
    ASSERT(node != nullptr, "Cannot erase null node.");
    GraphBuilder(&node->graph()).eraseNode(node);
    changed_ = true;
}

void RuntimeSourceGraphEditSession::link(LinkType type, Node *from, Node *to) {
    ASSERT(from != nullptr && to != nullptr, "Cannot link null nodes.");
    ASSERT(
        ownsGraph(&from->graph()) && ownsGraph(&to->graph()),
        "Runtime source edit link requires owned graphs.");
    NodeMutation::link(type, from, to);
    GraphBuilder(&from->graph()).touch();
    changed_ = true;
}

void RuntimeSourceGraphEditSession::addDependency(
    const camel::compile::gir::graph_ptr_t &owner,
    const camel::compile::gir::graph_ptr_t &dependency) {
    ASSERT(owner != nullptr && dependency != nullptr, "Cannot add null dependency.");
    GraphBuilder(owner).addDependency(dependency);
    changed_ = true;
}

camel::compile::gir::graph_ptr_t RuntimeSourceGraphEditSession::importDependency(
    const camel::compile::gir::graph_ptr_t &owner,
    const camel::compile::gir::graph_ptr_t &dependency, camel::compile::gir::GraphImportMode mode) {
    ASSERT(owner != nullptr && dependency != nullptr, "Cannot import null dependency.");
    auto imported = mode == camel::compile::gir::GraphImportMode::CloneIntoDraft
                        ? cloneSourceGraph(dependency)
                        : canonicalGraph(dependency);
    GraphBuilder(owner).addDependency(imported);
    changed_ = true;
    return imported;
}

void RuntimeSourceGraphEditSession::eraseDependency(
    const camel::compile::gir::graph_ptr_t &owner,
    const camel::compile::gir::graph_ptr_t &dependency) {
    ASSERT(owner != nullptr && dependency != nullptr, "Cannot erase null dependency.");
    GraphBuilder(owner).eraseDependency(dependency);
    changed_ = true;
}

void RuntimeSourceGraphEditSession::eraseSubGraph(
    const camel::compile::gir::graph_ptr_t &owner,
    const camel::compile::gir::graph_ptr_t &subGraph) {
    ASSERT(owner != nullptr && subGraph != nullptr, "Cannot erase null subgraph.");
    GraphBuilder(owner).eraseSubGraph(subGraph);
    changed_ = true;
}

camel::compile::gir::InlineResult RuntimeSourceGraphEditSession::inlineCallable(
    Node *node, const camel::compile::gir::InlineOptions &options) {
    ASSERT(node != nullptr, "Cannot inline null node.");
    auto result = GraphBuilder(&node->graph()).inlineCallable(node, options);
    changed_    = changed_ || static_cast<bool>(result);
    return result;
}

void RuntimeSourceGraphEditSession::adoptOwnedGraph(const camel::compile::gir::graph_ptr_t &graph) {
    ASSERT(graph != nullptr, "Cannot adopt null graph.");
    ownedGraphs_.insert(graph.get());
    changed_ = true;
}

camel::compile::gir::graph_ptr_t RuntimeSourceGraphEditSession::finish() {
    if (!changed_) {
        return sourceRoot_;
    }
    GraphBuilder::validateGraphRecursively(root_);
    GraphBuilder::sealGraphRecursively(root_);
    return root_;
}

void RuntimeSourceGraphEditSession::cloneReachableClosure(
    const std::vector<camel::compile::gir::graph_ptr_t> &reachableSourceGraphs) {
    ASSERT(!reachableSourceGraphs.empty(), "Runtime source edit session received empty closure.");
    for (const auto &sourceGraph : reachableSourceGraphs) {
        if (!sourceGraph || clonedBySource_.contains(sourceGraph.get())) {
            continue;
        }
        (void)cloneSourceGraph(sourceGraph);
    }
    auto rootIt = clonedBySource_.find(sourceRoot_.get());
    ASSERT(
        rootIt != clonedBySource_.end(),
        "Runtime source edit session did not clone root graph.");
    root_ = rootIt->second;
}

camel::compile::gir::graph_ptr_t RuntimeSourceGraphEditSession::cloneSourceGraph(
    const camel::compile::gir::graph_ptr_t &sourceGraph) {
    ASSERT(sourceGraph != nullptr, "Cannot clone null source graph.");
    if (auto it = clonedBySource_.find(sourceGraph.get()); it != clonedBySource_.end()) {
        return it->second;
    }
    std::unordered_map<const Node *, Node *> nodeMap;
    auto cloned = GraphBuilder::cloneGraph(sourceGraph, &nodeMap);
    ownedGraphs_.insert(cloned.get());
    clonedBySource_[sourceGraph.get()] = cloned;
    for (const auto &[src, dst] : nodeMap) {
        clonedNodeBySource_[src] = dst;
    }
    retargetGraphRegistries(cloned);
    retargetNodeGraphRefs(cloned);
    normalizeOwnedStaticValues(cloned);
    return cloned;
}

void RuntimeSourceGraphEditSession::retargetClonedClosure() {
    for (const auto &[_, graph] : clonedBySource_) {
        retargetGraphRegistries(graph);
    }
    for (const auto &[_, graph] : clonedBySource_) {
        retargetNodeGraphRefs(graph);
        normalizeOwnedStaticValues(graph);
    }
}

void RuntimeSourceGraphEditSession::retargetGraphRegistries(
    const camel::compile::gir::graph_ptr_t &graph) {
    GraphBuilder builder(graph);

    std::vector<camel::compile::gir::graph_ptr_t> deps(
        graph->dependencies().begin(),
        graph->dependencies().end());
    for (const auto &dep : deps) {
        builder.eraseDependency(dep);
    }
    for (const auto &dep : deps) {
        builder.addDependency(canonicalGraph(dep));
    }

    std::vector<camel::compile::gir::graph_ptr_t> subs;
    for (const auto &[_, group] : graph->subGraphs()) {
        for (const auto &sub : group) {
            subs.push_back(sub);
        }
    }
    for (const auto &sub : subs) {
        builder.eraseSubGraph(sub);
    }
    for (const auto &sub : subs) {
        builder.addSubGraph(canonicalGraph(sub));
    }
}

void RuntimeSourceGraphEditSession::retargetNodeGraphRefs(
    const camel::compile::gir::graph_ptr_t &graph) {
    for (Node *node : collectUseCarrierNodes(graph)) {
        if (!node) {
            continue;
        }
        if (node->type() == NodeType::FUNC) {
            auto *func = tt::as_ptr<FuncNode>(node);
            if (!func || !func->bodyGraph()) {
                continue;
            }
            auto bodyHandle = func->bodyGraph()->shared_from_this();
            auto mapped     = canonicalGraph(bodyHandle);
            if (mapped && mapped.get() != func->bodyGraph()) {
                NodeMutation::setBodyGraph(func, mapped.get());
            }
        }
    }
}

void RuntimeSourceGraphEditSession::normalizeOwnedStaticValues(
    const camel::compile::gir::graph_ptr_t &graph) {
    GraphBuilder ownerBuilder(graph);
    ObjectVisitSet visited;
    auto resolveTarget = [&](Graph *candidate) -> Graph * {
        if (!candidate) {
            return nullptr;
        }
        if (auto it = clonedBySource_.find(candidate); it != clonedBySource_.end()) {
            return it->second.get();
        }
        return candidate;
    };

    const auto *staticType = graph->staticDataType();
    if (!staticType) {
        return;
    }
    for (size_t i = 1; i < graph->staticDataSize() && i < staticType->size(); ++i) {
        (void)retargetStaticSlotGraphs(
            graph.get(),
            ownerBuilder,
            graph->getStaticDataSlot(-static_cast<camel::compile::gir::data_idx_t>(i)),
            const_cast<Type *>(staticType->typeAt(i)),
            resolveTarget,
            visited);
    }
}

} // namespace camel::runtime
