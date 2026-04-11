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
 * Created: Mar. 29, 2026
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir/draft.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"

#include <format>

namespace camel::compile::gir {

namespace {

using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Object;
using camel::core::type::ArrayType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;

using ObjectVisitSet = std::unordered_set<const Object *>;

graph_ptr_t requireOwnedGraphHandle(Graph *graph, const Graph *owner, const char *context) {
    ASSERT(graph != nullptr, "Graph handle request received a null graph.");
    try {
        return graph->shared_from_this();
    } catch (const std::bad_weak_ptr &) {
        throw std::runtime_error(
            std::format(
                "{} encountered non-owned graph '{}' ({:p}) while processing owner '{}'.",
                context,
                graph->name(),
                static_cast<void *>(graph),
                owner ? owner->name() : "<null>"));
    }
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
        auto *funcObj = fromSlot<StaticFunction *>(slot);
        if (!funcObj || !funcObj->graph()) {
            return false;
        }
        bool changed            = false;
        graph_ptr_t mappedGraph = resolveTarget(funcObj->graph());
        if (mappedGraph && mappedGraph.get() != funcObj->graph()) {
            funcObj->setGraph(mappedGraph.get());
            if (ownerRaw != mappedGraph.get() && !ownerRaw->dependencies().contains(mappedGraph)) {
                ownerBuilder.addDependency(mappedGraph);
            }
            changed = true;
        }

        if (::Tuple *closure = funcObj->tuple()) {
            TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
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
        auto *tupleType = static_cast<TupleType *>(type);
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

} // namespace

GraphDraft::GraphDraft(const graph_ptr_t &source) : sourceRoot_(source) {
    std::unordered_map<const Node *, Node *> nodeMap;
    root_ = GraphBuilder::cloneGraph(source, &nodeMap);
    registerNodeMap(nodeMap);
    registerOwnedGraph(sourceRoot_, root_);
    normalizeOwnedStaticValues(root_.get());
}

graph_ptr_t GraphDraft::canonicalGraph(const graph_ptr_t &graph) const {
    if (!graph) {
        return nullptr;
    }
    auto it = draftBySource_.find(graph.get());
    if (it != draftBySource_.end()) {
        return it->second;
    }
    return graph;
}

Node *GraphDraft::canonicalNode(const Node *node) const {
    if (!node) {
        return nullptr;
    }
    auto it = draftNodeBySource_.find(node);
    if (it != draftNodeBySource_.end()) {
        return it->second;
    }
    return const_cast<Node *>(node);
}

bool GraphDraft::hasDraftFor(const Graph *graph) const {
    return graph != nullptr && draftBySource_.contains(graph);
}

bool GraphDraft::hasDraftFor(const Node *node) const {
    return node != nullptr && draftNodeBySource_.contains(node);
}

bool GraphDraft::owns(const Graph *graph) const {
    return graph != nullptr && ownedGraphs_.contains(const_cast<Graph *>(graph));
}

void GraphDraft::adoptOwnedGraph(const graph_ptr_t &graph) {
    ASSERT(graph != nullptr, "Cannot adopt null graph into draft.");
    if (ownedGraphs_.insert(graph.get()).second) {
        dirtyGraphs_.insert(graph.get());
    }
}

graph_ptr_t GraphDraft::cloneIntoDraft(const graph_ptr_t &graph) {
    ASSERT(graph != nullptr, "Cannot clone null graph into draft.");
    auto existing = draftBySource_.find(graph.get());
    if (existing != draftBySource_.end()) {
        return existing->second;
    }
    std::unordered_map<const Node *, Node *> nodeMap;
    graph_ptr_t cloned = GraphBuilder::cloneGraph(graph, &nodeMap);
    registerNodeMap(nodeMap);
    registerOwnedGraph(graph, cloned);
    // First retarget references to known drafts inside the newly cloned graph, then perform the
    // source->draft replacement across the whole owned graph domain. This order avoids missing
    // cross-graph edges such as A(new clone) -> B(already cloned).
    retargetKnownDraftRefsInOwner(cloned.get());
    retargetOwnedGraphRefs(graph, cloned);
    normalizeOwnedStaticValues(cloned.get());
    return cloned;
}

graph_ptr_t GraphDraft::importSubGraph(
    const graph_ptr_t &owner, const graph_ptr_t &subGraph, GraphImportMode mode) {
    ASSERT(owner != nullptr && subGraph != nullptr, "Cannot import null subgraph.");
    assertDraftOwned(owner.get(), "import subgraph into");
    graph_ptr_t imported = resolveImportTarget(subGraph, mode);
    GraphBuilder(owner).addSubGraph(imported);
    markDirty(owner.get());
    return imported;
}

graph_ptr_t GraphDraft::importDependency(
    const graph_ptr_t &owner, const graph_ptr_t &dep, GraphImportMode mode) {
    ASSERT(owner != nullptr && dep != nullptr, "Cannot import null dependency.");
    assertDraftOwned(owner.get(), "import dependency into");
    graph_ptr_t imported = resolveImportTarget(dep, mode);
    GraphBuilder(owner).addDependency(imported);
    markDirty(owner.get());
    return imported;
}

void GraphDraft::eraseNode(Node *node) {
    Graph *ownerGraph = &node->graph();
    assertDraftOwned(ownerGraph, "erase node from");
    GraphBuilder(ownerGraph).eraseNode(node);
    markDirty(ownerGraph);
}

bool GraphDraft::replaceNode(Node *oldNode, Node *newNode) {
    Graph *ownerGraph = &oldNode->graph();
    assertDraftOwned(ownerGraph, "replace node on");
    assertDraftOwned(&newNode->graph(), "replace node on");
    bool changed = detail::NodeMutation::replace(oldNode, newNode);
    GraphBuilder(ownerGraph).touch();
    markDirty(ownerGraph);
    eraseNode(oldNode);
    return changed;
}

bool GraphDraft::replaceAllUses(Node *oldNode, Node *newNode) {
    Graph *ownerGraph = &oldNode->graph();
    assertDraftOwned(ownerGraph, "replace uses on");
    assertDraftOwned(&newNode->graph(), "replace uses on");
    bool changed = detail::NodeMutation::replaceUses(oldNode, newNode);
    GraphBuilder(ownerGraph).touch();
    markDirty(ownerGraph);
    return changed;
}

void GraphDraft::addDependency(const graph_ptr_t &owner, const graph_ptr_t &dep) {
    importDependency(owner, dep, GraphImportMode::ReferenceOnly);
}

void GraphDraft::eraseDependency(const graph_ptr_t &owner, const graph_ptr_t &dep) {
    assertDraftOwned(owner.get(), "erase dependency from");
    GraphBuilder(owner).eraseDependency(dep);
    markDirty(owner.get());
}

void GraphDraft::retargetDependency(
    const graph_ptr_t &owner, const graph_ptr_t &oldDep, const graph_ptr_t &newDep,
    GraphImportMode mode) {
    ASSERT(
        owner != nullptr && oldDep != nullptr && newDep != nullptr,
        "Cannot retarget null dependency.");
    graph_ptr_t imported = importDependency(owner, newDep, mode);
    eraseDependency(owner, oldDep);
    markDirty(owner.get());
    (void)imported;
}

void GraphDraft::eraseSubGraph(const graph_ptr_t &owner, const graph_ptr_t &subGraph) {
    assertDraftOwned(owner.get(), "erase subgraph from");
    GraphBuilder(owner).eraseSubGraph(subGraph);
    markDirty(owner.get());
}

void GraphDraft::link(LinkType type, Node *from, Node *to) {
    assertDraftOwned(&from->graph(), "link from");
    assertDraftOwned(&to->graph(), "link to");
    detail::NodeMutation::link(type, from, to);
    GraphBuilder(&from->graph()).touch();
    markDirty(&from->graph());
}

void GraphDraft::replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput) {
    assertDraftOwned(&owner->graph(), "replace input on");
    assertDraftOwned(&oldInput->graph(), "replace input on");
    assertDraftOwned(&newInput->graph(), "replace input on");
    detail::NodeMutation::replaceInput(type, owner, oldInput, newInput);
    GraphBuilder(&owner->graph()).touch();
    markDirty(&owner->graph());
}

void GraphDraft::replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput) {
    assertDraftOwned(&owner->graph(), "replace output on");
    assertDraftOwned(&oldOutput->graph(), "replace output on");
    assertDraftOwned(&newOutput->graph(), "replace output on");
    detail::NodeMutation::replaceOutput(type, owner, oldOutput, newOutput);
    GraphBuilder(&owner->graph()).touch();
    markDirty(&owner->graph());
}

InlineResult GraphDraft::inlineCallable(Node *node, const InlineOptions &options) {
    Graph *ownerGraph = &node->graph();
    assertDraftOwned(ownerGraph, "inline into");
    InlineResult result = GraphBuilder(ownerGraph).inlineCallable(node, options);
    if (result) {
        markDirty(ownerGraph);
    }
    return result;
}

void GraphDraft::pruneUnreachable(const graph_ptr_t &graph) {
    ASSERT(graph != nullptr, "Cannot prune null graph.");
    assertDraftOwned(graph.get(), "prune graph");
    GraphBuilder(graph).pruneUnreachable();
    markDirty(graph.get());
}

void GraphDraft::normalizeAllOwnedStaticValues() {
    for (Graph *ownerRaw : ownedGraphs_) {
        normalizeOwnedStaticValues(ownerRaw);
    }
}

void GraphDraft::validate() const { GraphBuilder::validateGraphRecursively(root_); }

void GraphDraft::seal() {
    validate();
    dirtyGraphs_.clear();
    GraphBuilder::sealGraphRecursively(root_);
}

void GraphDraft::registerOwnedGraph(const graph_ptr_t &sourceGraph, const graph_ptr_t &draftGraph) {
    if (!sourceGraph || !draftGraph) {
        return;
    }
    ownedGraphs_.insert(draftGraph.get());
    draftBySource_[sourceGraph.get()] = draftGraph;
    sourceByDraft_[draftGraph.get()]  = sourceGraph;
}

void GraphDraft::registerNodeMap(const std::unordered_map<const Node *, Node *> &nodeMap) {
    for (const auto &[sourceNode, draftNode] : nodeMap) {
        draftNodeBySource_[sourceNode] = draftNode;
    }
}

graph_ptr_t GraphDraft::resolveImportTarget(const graph_ptr_t &graph, GraphImportMode mode) {
    if (owns(graph.get())) {
        return graph;
    }
    if (mode == GraphImportMode::CloneIntoDraft) {
        return cloneIntoDraft(graph);
    }
    auto existing = draftBySource_.find(graph.get());
    if (existing != draftBySource_.end()) {
        return existing->second;
    }
    return graph;
}

graph_ptr_t GraphDraft::sourceGraphOfDraft(const Graph *draftGraph) const {
    if (!draftGraph) {
        return nullptr;
    }
    auto it = sourceByDraft_.find(draftGraph);
    if (it != sourceByDraft_.end()) {
        return it->second;
    }
    return nullptr;
}

void GraphDraft::normalizeOwnedStaticValues(Graph *ownerRaw) {
    if (!ownerRaw || !ownerRaw->staticDataType()) {
        return;
    }

    bool changed = false;
    GraphBuilder ownerBuilder(ownerRaw);
    ObjectVisitSet visited;
    auto resolveTarget = [&](Graph *graph) -> graph_ptr_t {
        if (!graph) {
            return nullptr;
        }
        auto it = draftBySource_.find(graph);
        if (it != draftBySource_.end()) {
            return it->second;
        }
        return requireOwnedGraphHandle(graph, ownerRaw, "GraphDraft::normalizeOwnedStaticValues");
    };

    const TupleType *staticType = ownerRaw->staticDataType();
    for (size_t i = 1; i < ownerRaw->staticDataSize() && i < staticType->size(); ++i) {
        changed = retargetStaticSlotGraphs(
                      ownerRaw,
                      ownerBuilder,
                      ownerRaw->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                      staticType->typeAt(i),
                      resolveTarget,
                      visited) ||
                  changed;
    }

    if (changed) {
        markDirty(ownerRaw);
    }
}

void GraphDraft::retargetOwnedGraphRefs(
    const graph_ptr_t &sourceGraph, const graph_ptr_t &draftGraph) {
    ASSERT(sourceGraph != nullptr && draftGraph != nullptr, "Cannot retarget null graph refs.");
    Graph *sourceRaw = sourceGraph.get();
    Graph *draftRaw  = draftGraph.get();
    if (sourceRaw == draftRaw) {
        return;
    }

    for (Graph *ownerRaw : ownedGraphs_) {
        if (!ownerRaw) {
            continue;
        }
        bool changed = false;

        std::vector<graph_ptr_t> staleDeps;
        for (const auto &dep : ownerRaw->dependencies()) {
            if (dep.get() == sourceRaw) {
                staleDeps.push_back(dep);
            }
        }
        if (!staleDeps.empty()) {
            GraphBuilder ownerBuilder(ownerRaw);
            for (const auto &dep : staleDeps) {
                ownerBuilder.eraseDependency(dep);
                changed = true;
            }
            if (!ownerRaw->dependencies().contains(draftGraph)) {
                ownerBuilder.addDependency(draftGraph);
                changed = true;
            }
        }

        bool subGraphReferenced = false;
        for (const auto &[_, subGraphs] : ownerRaw->subGraphs()) {
            if (subGraphs.contains(sourceGraph)) {
                subGraphReferenced = true;
                break;
            }
        }
        if (subGraphReferenced) {
            GraphBuilder ownerBuilder(ownerRaw);
            ownerBuilder.eraseSubGraph(sourceGraph);
            // `addSubGraph()` writes graph->outer. Only rebuild the subgraph relation when the
            // owner is truly the source's lexical outer scope, otherwise alias registration would
            // be promoted into a real nesting relation.
            graph_ptr_t ownerSource = sourceGraphOfDraft(ownerRaw);
            const auto sourceOuter  = sourceGraph->outer();
            const bool ownerIsLexicalOuter =
                sourceOuter && ownerSource && sourceOuter.get() == ownerSource.get();
            if (ownerIsLexicalOuter && !draftGraph->name().empty()) {
                ownerBuilder.addSubGraph(draftGraph);
            }
            changed = true;
        }

        for (Node *node : ownerRaw->nodes()) {
            if (node->type() == NodeType::FUNC) {
                auto *func = tt::as_ptr<FuncNode>(node);
                if (func && func->bodyGraph() == sourceRaw) {
                    detail::NodeMutation::setBodyGraph(func, draftRaw);
                    GraphBuilder ownerBuilder(ownerRaw);
                    ensureDependencyRegistration(ownerRaw, draftGraph, ownerBuilder);
                    changed = true;
                }
            }
        }

        GraphBuilder ownerBuilder(ownerRaw);
        ObjectVisitSet visited;
        auto resolveTarget = [&](Graph *graph) -> graph_ptr_t {
            if (graph == sourceRaw) {
                return draftGraph;
            }
            return graph ? requireOwnedGraphHandle(
                               graph,
                               ownerRaw,
                               "GraphDraft::retargetOwnedGraphRefs")
                         : nullptr;
        };
        const TupleType *staticType = ownerRaw->staticDataType();
        if (staticType) {
            for (size_t i = 1; i < ownerRaw->staticDataSize() && i < staticType->size(); ++i) {
                changed = retargetStaticSlotGraphs(
                              ownerRaw,
                              ownerBuilder,
                              ownerRaw->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                              staticType->typeAt(i),
                              resolveTarget,
                              visited) ||
                          changed;
            }
        }

        if (changed) {
            markDirty(ownerRaw);
        }
    }
}

void GraphDraft::retargetKnownDraftRefsInOwner(Graph *ownerRaw) {
    if (!ownerRaw) {
        return;
    }
    bool changed = false;
    GraphBuilder ownerBuilder(ownerRaw);

    std::vector<std::pair<graph_ptr_t, graph_ptr_t>> depRetargets;
    for (const auto &dep : ownerRaw->dependencies()) {
        auto it = draftBySource_.find(dep.get());
        if (it == draftBySource_.end()) {
            continue;
        }
        const graph_ptr_t &draftDep = it->second;
        if (draftDep.get() == dep.get()) {
            continue;
        }
        depRetargets.emplace_back(dep, draftDep);
    }
    for (const auto &[oldDep, newDep] : depRetargets) {
        ownerBuilder.eraseDependency(oldDep);
        if (!ownerRaw->dependencies().contains(newDep)) {
            ownerBuilder.addDependency(newDep);
        }
        changed = true;
    }

    std::vector<std::pair<graph_ptr_t, graph_ptr_t>> subRetargets;
    for (const auto &[_, subGraphs] : ownerRaw->subGraphs()) {
        for (const auto &sub : subGraphs) {
            auto it = draftBySource_.find(sub.get());
            if (it == draftBySource_.end()) {
                continue;
            }
            const graph_ptr_t &draftSub = it->second;
            if (draftSub.get() == sub.get()) {
                continue;
            }
            subRetargets.emplace_back(sub, draftSub);
        }
    }
    for (const auto &[oldSub, newSub] : subRetargets) {
        ownerBuilder.eraseSubGraph(oldSub);
        graph_ptr_t ownerSource = sourceGraphOfDraft(ownerRaw);
        const auto oldOuter     = oldSub->outer();
        const bool ownerIsLexicalOuter =
            oldOuter && ownerSource && oldOuter.get() == ownerSource.get();
        if (ownerIsLexicalOuter && !newSub->name().empty()) {
            ownerBuilder.addSubGraph(newSub);
        }
        changed = true;
    }

    for (Node *node : ownerRaw->nodes()) {
        if (node->type() == NodeType::FUNC) {
            auto *func = tt::as_ptr<FuncNode>(node);
            if (!func || !func->bodyGraph()) {
                continue;
            }
            auto it = draftBySource_.find(func->bodyGraph());
            if (it == draftBySource_.end()) {
                continue;
            }
            if (it->second.get() != func->bodyGraph()) {
                detail::NodeMutation::setBodyGraph(func, it->second.get());
                ensureDependencyRegistration(ownerRaw, it->second, ownerBuilder);
                changed = true;
            }
            continue;
        }
    }

    ObjectVisitSet visited;
    auto resolveTarget = [&](Graph *graph) -> graph_ptr_t {
        if (!graph) {
            return nullptr;
        }
        auto it = draftBySource_.find(graph);
        if (it == draftBySource_.end()) {
            return requireOwnedGraphHandle(
                graph,
                ownerRaw,
                "GraphDraft::retargetKnownDraftRefsInOwner");
        }
        return it->second;
    };
    const TupleType *staticType = ownerRaw->staticDataType();
    if (staticType) {
        for (size_t i = 1; i < ownerRaw->staticDataSize() && i < staticType->size(); ++i) {
            changed = retargetStaticSlotGraphs(
                          ownerRaw,
                          ownerBuilder,
                          ownerRaw->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                          staticType->typeAt(i),
                          resolveTarget,
                          visited) ||
                      changed;
        }
    }

    if (changed) {
        markDirty(ownerRaw);
    }
}

void GraphDraft::ensureDependencyRegistration(
    Graph *ownerRaw, const graph_ptr_t &dep, GraphBuilder &ownerBuilder) {
    if (!ownerRaw || !dep) {
        return;
    }
    if (ownerRaw == dep.get()) {
        // Self dependency does not enter the dependencies set in GraphBuilder, but it does set
        // looped=true for recursive graph semantics, so we must trigger it explicitly here.
        ownerBuilder.addDependency(dep);
        return;
    }
    if (!ownerRaw->dependencies().contains(dep)) {
        ownerBuilder.addDependency(dep);
    }
}

void GraphDraft::assertDraftOwned(const Graph *graph, const char *action) const {
    ASSERT(
        owns(graph),
        std::format(
            "GraphDraft cannot {} non-owned graph '{}'. Import/clone it into the draft first.",
            action,
            graph ? graph->name() : "<null>"));
}

void GraphDraft::markDirty(Graph *graph) { dirtyGraphs_.insert(graph); }

} // namespace camel::compile::gir
