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
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir/draft.h"

#include <format>

namespace camel::compile::gir {

GraphDraft::GraphDraft(const graph_ptr_t &source)
    : sourceRoot_(source), root_(GraphBuilder::cloneGraph(source)) {
    registerOwnedGraphs(root_);
    if (sourceRoot_) {
        draftBySource_[sourceRoot_.get()] = root_;
    }
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

bool GraphDraft::hasDraftFor(const Graph *graph) const {
    return graph != nullptr && draftBySource_.contains(graph);
}

bool GraphDraft::owns(const Graph *graph) const {
    return graph != nullptr && ownedGraphs_.contains(const_cast<Graph *>(graph));
}

graph_ptr_t GraphDraft::cloneIntoDraft(const graph_ptr_t &graph) {
    ASSERT(graph != nullptr, "Cannot clone null graph into draft.");
    auto existing = draftBySource_.find(graph.get());
    if (existing != draftBySource_.end()) {
        return existing->second;
    }
    graph_ptr_t cloned = GraphBuilder::cloneGraph(graph);
    registerOwnedGraphs(cloned);
    draftBySource_[graph.get()] = cloned;
    // 先修正“新克隆图内部”对已知 draft 的引用，再做“全 owned 图域”的 source->draft 替换。
    // 这个顺序保证不会遗漏类似 A(新克隆) -> B(已克隆) 的跨图边。
    retargetKnownDraftRefsInOwner(cloned.get());
    retargetOwnedGraphRefs(graph, cloned);
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

void GraphDraft::validate() const { GraphBuilder::validateGraphRecursively(root_); }

void GraphDraft::seal() {
    validate();
    dirtyGraphs_.clear();
    GraphBuilder::sealGraphRecursively(root_);
}

void GraphDraft::registerOwnedGraphs(const graph_ptr_t &graph) {
    if (!graph) {
        return;
    }
    ownedGraphs_.insert(graph.get());
}

graph_ptr_t GraphDraft::resolveImportTarget(const graph_ptr_t &graph, GraphImportMode mode) {
    if (mode == GraphImportMode::CloneIntoDraft) {
        return cloneIntoDraft(graph);
    }
    auto existing = draftBySource_.find(graph.get());
    if (existing != draftBySource_.end()) {
        return existing->second;
    }
    return graph;
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
            // `addSubGraph()` 会写入 graph->outer。仅当 owner 确实是 source 的词法外层时
            // 才允许重建子图关系，避免把“别名注册”误升级为真实嵌套关系。
            const auto sourceOuter         = sourceGraph->outer();
            const bool ownerIsLexicalOuter = sourceOuter && sourceOuter.get() == ownerRaw;
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
            } else if (node->type() == NodeType::DATA) {
                auto *dataNode = tt::as_ptr<DataNode>(node);
                if (!dataNode ||
                    dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
                    continue;
                }
                auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
                if (funcObj && funcObj->graph() == sourceRaw) {
                    funcObj->setGraph(draftRaw);
                    GraphBuilder ownerBuilder(ownerRaw);
                    ensureDependencyRegistration(ownerRaw, draftGraph, ownerBuilder);
                    changed = true;
                }
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
        const auto oldOuter            = oldSub->outer();
        const bool ownerIsLexicalOuter = oldOuter && oldOuter.get() == ownerRaw;
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
        if (node->type() != NodeType::DATA) {
            continue;
        }
        auto *dataNode = tt::as_ptr<DataNode>(node);
        if (!dataNode || dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
            continue;
        }
        auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
        if (!funcObj || !funcObj->graph()) {
            continue;
        }
        auto it = draftBySource_.find(funcObj->graph());
        if (it == draftBySource_.end()) {
            continue;
        }
        if (it->second.get() != funcObj->graph()) {
            funcObj->setGraph(it->second.get());
            ensureDependencyRegistration(ownerRaw, it->second, ownerBuilder);
            changed = true;
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
        // self dependency 在 GraphBuilder 里不会进入 dependencies 集合，
        // 但会设置 looped=true（递归图语义），这里必须显式触发一次。
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
