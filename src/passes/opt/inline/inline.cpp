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
 * Created: Oct. 25, 2025
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "inline.h"

#include "camel/compile/gir/rewrite.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/func.h"
#include "camel/utils/log.h"

using namespace std;
using namespace GIR;
using namespace camel::core::error;

namespace {

constexpr size_t kInlineTargetBudgetPerScc = 20000;
constexpr size_t kInlineApplyBudgetPerScc  = 20000;
constexpr size_t kNodeDeltaBudgetPerScc    = 50000;
// 预算语义：
// - Debug: 触发预算即 ASSERT，作为正确性哨兵，强制暴露潜在失控路径。
// - Release: 仅保护性告警并中止当前 SCC，避免线上无限膨胀。

graph_ptr_t ensureEditableGraph(
    GraphRewriteSession &session, const graph_ptr_t &sourceRoot, const graph_ptr_t &sourceGraph,
    std::unordered_map<const Graph *, graph_ptr_t> &cache);

bool hasSubGraphRef(const graph_ptr_t &owner, const graph_ptr_t &candidate) {
    for (const auto &[_, subGraphs] : owner->subGraphs()) {
        if (subGraphs.contains(candidate)) {
            return true;
        }
    }
    return false;
}

void collectReachableGraphs(
    const graph_ptr_t &graph, std::vector<graph_ptr_t> &out,
    std::unordered_set<const Graph *> &visited) {
    if (!graph || !visited.insert(graph.get()).second) {
        return;
    }
    out.push_back(graph);
    for (const auto &[_, subGraphs] : graph->subGraphs()) {
        for (const auto &subGraph : subGraphs) {
            collectReachableGraphs(subGraph, out, visited);
        }
    }
    for (const auto &dep : graph->dependencies()) {
        collectReachableGraphs(dep, out, visited);
    }
}

enum class UseKind {
    FuncBody,
    DataFunction,
};

struct GraphUseSite {
    graph_ptr_t owner;
    Node *node = nullptr;
    UseKind kind{};
};

using GraphUseIndex = std::unordered_map<const Graph *, std::vector<GraphUseSite>>;

struct InlineTarget {
    graph_ptr_t owner;
    Node *branch   = nullptr;
    Node *path     = nullptr;
    JoinNode *join = nullptr;
    graph_ptr_t bodyGraph;
};

struct SccGraphPlan {
    std::vector<std::vector<const Graph *>> components;
    std::unordered_map<const Graph *, size_t> componentOf;
    std::vector<std::vector<size_t>> dag;
    std::vector<size_t> processOrder;
};

std::vector<const Graph *> collectReferencedGraphs(const graph_ptr_t &graph) {
    std::vector<const Graph *> refs;
    refs.reserve(graph->dependencies().size() + graph->subGraphs().size() + graph->nodes().size());
    for (const auto &[_, subGraphs] : graph->subGraphs()) {
        for (const auto &sub : subGraphs) {
            refs.push_back(sub.get());
        }
    }
    for (const auto &dep : graph->dependencies()) {
        refs.push_back(dep.get());
    }
    for (Node *node : graph->nodes()) {
        if (node->type() == NodeType::FUNC) {
            auto *func = tt::as_ptr<FuncNode>(node);
            if (func->bodyGraph()) {
                refs.push_back(func->bodyGraph());
            }
            continue;
        }
        if (node->type() != NodeType::DATA) {
            continue;
        }
        auto *dataNode = tt::as_ptr<DataNode>(node);
        if (dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
            continue;
        }
        auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
        if (funcObj && funcObj->graph()) {
            refs.push_back(funcObj->graph());
        }
    }
    return refs;
}

GraphUseIndex buildUseIndex(const std::vector<graph_ptr_t> &draftGraphs) {
    // 会话级 use-site 索引：
    // calleeGraph -> [(ownerGraph, useNode, kind)]。
    // 作为 legacy->draft 重定向与 dependency 同步的唯一批处理入口。
    GraphUseIndex index;
    for (const auto &owner : draftGraphs) {
        for (Node *node : owner->nodes()) {
            if (node->type() == NodeType::FUNC) {
                auto *func = tt::as_ptr<FuncNode>(node);
                if (func->bodyGraph()) {
                    index[func->bodyGraph()].push_back(
                        GraphUseSite{
                            .owner = owner,
                            .node  = node,
                            .kind  = UseKind::FuncBody,
                        });
                }
                continue;
            }
            if (node->type() != NodeType::DATA) {
                continue;
            }
            auto *dataNode = tt::as_ptr<DataNode>(node);
            if (dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
                continue;
            }
            auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
            if (!funcObj || !funcObj->graph()) {
                continue;
            }
            index[funcObj->graph()].push_back(
                GraphUseSite{
                    .owner = owner,
                    .node  = node,
                    .kind  = UseKind::DataFunction,
                });
        }
    }
    return index;
}

void retargetLegacyRefsByUseIndex(
    GraphRewriteSession &session, const graph_ptr_t &sourceRoot, const GraphUseIndex &useIndex,
    std::unordered_map<const Graph *, graph_ptr_t> &cache) {
    // 这里不做“局部就地猜测修补”，而是按 use-index 一次性重定向全部引用，
    // 再统一同步 ownerGraph 的 dependency，避免后续 seal 出现悬挂 legacy 引用。
    for (const auto &[legacyGraph, useSites] : useIndex) {
        auto legacyIt = cache.find(legacyGraph);
        graph_ptr_t draftRef;
        if (legacyIt != cache.end()) {
            draftRef = legacyIt->second;
        } else {
            draftRef = ensureEditableGraph(
                session,
                sourceRoot,
                const_cast<Graph *>(legacyGraph)->shared_from_this(),
                cache);
        }
        graph_ptr_t legacyRef = const_cast<Graph *>(legacyGraph)->shared_from_this();
        if (legacyRef == draftRef) {
            continue;
        }
        for (const auto &use : useSites) {
            Graph *ownerRaw = use.owner.get();
            if (!ownerRaw) {
                continue;
            }
            if (use.kind == UseKind::FuncBody) {
                auto *funcNode = tt::as_ptr<FuncNode>(use.node);
                if (!funcNode || !funcNode->bodyGraph()) {
                    continue;
                }
                if (funcNode->bodyGraph() != draftRef.get()) {
                    funcNode->setBodyGraph(draftRef.get());
                }
            } else {
                auto *dataNode = tt::as_ptr<DataNode>(use.node);
                if (!dataNode ||
                    dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
                    continue;
                }
                auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
                if (!funcObj || !funcObj->graph()) {
                    continue;
                }
                funcObj->setGraph(draftRef.get());
            }
            if (use.owner->dependencies().contains(legacyRef)) {
                session.eraseDependency(use.owner, legacyRef);
            }
            if (!use.owner->dependencies().contains(draftRef)) {
                session.importDependency(use.owner, draftRef);
            }
            EXEC_WHEN_DEBUG(
                GetDefaultLogger()
                    .in("InlinePass")
                    .debug(
                        "Retargeted graph use in owner {}: {} -> {}.",
                        use.owner->name(),
                        legacyRef->name(),
                        draftRef->name()));
            (void)ownerRaw;
        }
    }
}

void assertNoLegacyGraphRefs(
    const std::vector<graph_ptr_t> &draftGraphs,
    const std::unordered_set<const Graph *> &legacySources) {
    for (const auto &owner : draftGraphs) {
        for (Node *node : owner->nodes()) {
            if (node->type() == NodeType::FUNC) {
                auto *func = tt::as_ptr<FuncNode>(node);
                if (!func->bodyGraph()) {
                    continue;
                }
                ASSERT(
                    !legacySources.contains(func->bodyGraph()),
                    std::format(
                        "Legacy FUNC.bodyGraph '{}' still referenced in draft graph '{}'.",
                        func->bodyGraph()->name(),
                        owner->name()));
            } else if (node->type() == NodeType::DATA) {
                auto *data = tt::as_ptr<DataNode>(node);
                if (data->dataType()->code() != camel::core::type::TypeCode::Function) {
                    continue;
                }
                auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(data->dataSlot());
                if (!funcObj || !funcObj->graph()) {
                    continue;
                }
                ASSERT(
                    !legacySources.contains(funcObj->graph()),
                    std::format(
                        "Legacy DATA(Function).graph '{}' still referenced in draft graph '{}'.",
                        funcObj->graph()->name(),
                        owner->name()));
            }
        }
    }
}

SccGraphPlan buildSccGraphPlan(const std::vector<graph_ptr_t> &sourceGraphs) {
    SccGraphPlan plan;
    std::unordered_map<const Graph *, graph_ptr_t> sourceByPtr;
    for (const auto &g : sourceGraphs) {
        sourceByPtr.emplace(g.get(), g);
    }

    std::unordered_map<const Graph *, std::vector<const Graph *>> adjacency;
    for (const auto &g : sourceGraphs) {
        std::unordered_set<const Graph *> uniqueRefs;
        for (const Graph *ref : collectReferencedGraphs(g)) {
            if (sourceByPtr.contains(ref) && ref != g.get()) {
                uniqueRefs.insert(ref);
            }
        }
        adjacency[g.get()] = std::vector<const Graph *>(uniqueRefs.begin(), uniqueRefs.end());
    }

    std::unordered_map<const Graph *, size_t> index;
    std::unordered_map<const Graph *, size_t> low;
    std::unordered_set<const Graph *> onStack;
    std::vector<const Graph *> stack;
    size_t nextIndex = 0;

    std::function<void(const Graph *)> dfs = [&](const Graph *v) {
        index[v] = nextIndex;
        low[v]   = nextIndex;
        nextIndex++;
        stack.push_back(v);
        onStack.insert(v);

        for (const Graph *w : adjacency[v]) {
            if (!index.contains(w)) {
                dfs(w);
                low[v] = std::min(low[v], low[w]);
            } else if (onStack.contains(w)) {
                low[v] = std::min(low[v], index[w]);
            }
        }

        if (low[v] == index[v]) {
            std::vector<const Graph *> component;
            while (!stack.empty()) {
                const Graph *node = stack.back();
                stack.pop_back();
                onStack.erase(node);
                component.push_back(node);
                if (node == v) {
                    break;
                }
            }
            size_t compId = plan.components.size();
            for (const Graph *node : component) {
                plan.componentOf[node] = compId;
            }
            plan.components.push_back(std::move(component));
        }
    };

    for (const auto &g : sourceGraphs) {
        if (!index.contains(g.get())) {
            dfs(g.get());
        }
    }

    plan.dag.assign(plan.components.size(), {});
    std::vector<std::unordered_set<size_t>> dedup(plan.components.size());
    for (const auto &g : sourceGraphs) {
        size_t fromComp = plan.componentOf.at(g.get());
        for (const Graph *ref : adjacency[g.get()]) {
            size_t toComp = plan.componentOf.at(ref);
            if (fromComp == toComp || dedup[fromComp].contains(toComp)) {
                continue;
            }
            dedup[fromComp].insert(toComp);
            plan.dag[fromComp].push_back(toComp);
        }
    }

    std::vector<size_t> indegree(plan.components.size(), 0);
    for (size_t u = 0; u < plan.dag.size(); ++u) {
        for (size_t v : plan.dag[u]) {
            indegree[v]++;
        }
    }
    std::deque<size_t> q;
    for (size_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            q.push_back(i);
        }
    }
    std::vector<size_t> topo;
    topo.reserve(plan.components.size());
    while (!q.empty()) {
        size_t u = q.front();
        q.pop_front();
        topo.push_back(u);
        for (size_t v : plan.dag[u]) {
            indegree[v]--;
            if (indegree[v] == 0) {
                q.push_back(v);
            }
        }
    }
    ASSERT(
        topo.size() == plan.components.size(),
        "SCC condensed graph must be a DAG for topological ordering.");
    // 注意：边方向定义为 caller -> callee。
    // 因此逆拓扑序即 “callee-first / bottom-up”，可保证先处理最深层被引用图。
    plan.processOrder = std::vector<size_t>(topo.rbegin(), topo.rend());
    return plan;
}

std::vector<InlineTarget> collectInitialInlineTargetsForScc(
    size_t sccId, const SccGraphPlan &plan,
    const std::unordered_map<const Graph *, graph_ptr_t> &editableBySource) {
    // SCC 内“初始目标冻结”：
    // 本轮只处理进入 SCC 时已存在的 FUNC 目标；
    // 内联过程中新增的 FUNC 留给后续 pass 轮次，避免单轮振荡/爆炸。
    std::vector<InlineTarget> targets;
    for (const Graph *sourceGraph : plan.components[sccId]) {
        graph_ptr_t g = editableBySource.at(sourceGraph);
        for (Node *brch : g->nodes()) {
            if (brch->type() != NodeType::BRCH) {
                continue;
            }
            auto *brchNode = tt::as_ptr<BrchNode>(brch);
            for (size_t i = 0; i < brchNode->armCount(); ++i) {
                Node *path = brchNode->armHead(i);
                if (path->type() != NodeType::FUNC) {
                    continue;
                }
                auto *funcPath = tt::as_ptr<FuncNode>(path);
                if (!funcPath->hasMatchedJoin() || !funcPath->bodyGraph()) {
                    continue;
                }
                targets.push_back(
                    InlineTarget{
                        .owner     = g,
                        .branch    = brch,
                        .path      = path,
                        .join      = funcPath->matchedJoin(),
                        .bodyGraph = funcPath->bodyGraph()->shared_from_this(),
                    });
            }
        }
    }
    return targets;
}

void assertAdjacencyPointersBelongToGraph(const graph_ptr_t &graph) {
    std::unordered_set<Node *> liveNodes;
    liveNodes.reserve(graph->nodes().size() + graph->ports().size() + graph->closure().size() + 1);
    for (Node *n : graph->nodes()) {
        liveNodes.insert(n);
    }
    for (Node *n : graph->ports()) {
        liveNodes.insert(n);
    }
    for (Node *n : graph->closure()) {
        liveNodes.insert(n);
    }
    if (graph->hasOutput()) {
        liveNodes.insert(graph->exitNode());
    }

    auto assertNodeList =
        [&](Node *owner, const auto &nodes, const char *dir, const char *linkType) {
            for (Node *peer : nodes) {
                std::string peerGraphName = "<unknown>";
                std::string peerNodeStr   = "<unknown>";
                if (peer) {
                    peerGraphName = peer->graph().name();
                    peerNodeStr   = peer->toString();
                }
                ASSERT(
                    liveNodes.contains(peer),
                    std::format(
                        "Graph '{}' has stale {} {} pointer: owner={}, peer={:p}, peerNode={}, "
                        "peerGraph={}.",
                        graph->name(),
                        linkType,
                        dir,
                        owner ? owner->toString() : "<null>",
                        static_cast<void *>(peer),
                        peerNodeStr,
                        peerGraphName));
            }
        };

    for (Node *owner : graph->nodes()) {
        assertNodeList(owner, owner->normInputs(), "input", "norm");
        assertNodeList(owner, owner->withInputs(), "input", "with");
        assertNodeList(owner, owner->ctrlInputs(), "input", "ctrl");
        assertNodeList(owner, owner->normOutputs(), "output", "norm");
        assertNodeList(owner, owner->withOutputs(), "output", "with");
        assertNodeList(owner, owner->ctrlOutputs(), "output", "ctrl");
    }
}

std::unordered_set<Node *> collectLiveNodesFromExit(const graph_ptr_t &graph) {
    std::unordered_set<Node *> live;
    if (!graph || !graph->hasOutput()) {
        return live;
    }
    std::vector<Node *> stack{graph->exitNode()};
    while (!stack.empty()) {
        Node *curr = stack.back();
        stack.pop_back();
        if (!curr || !live.insert(curr).second) {
            continue;
        }
        for (Node *in : curr->withInputs()) {
            stack.push_back(in);
        }
        for (Node *in : curr->normInputs()) {
            stack.push_back(in);
        }
        for (Node *in : curr->ctrlInputs()) {
            stack.push_back(in);
        }
    }
    return live;
}

std::unordered_set<Node *> collectBranchSlotPinnedNodes(const graph_ptr_t &graph) {
    std::unordered_set<Node *> pinned;
    if (!graph) {
        return pinned;
    }
    for (Node *node : graph->nodes()) {
        if (node->type() == NodeType::BRCH) {
            pinned.insert(node);
            for (Node *armHead : node->ctrlOutputs()) {
                if (armHead) {
                    pinned.insert(armHead);
                }
            }
            continue;
        }
        if (node->type() == NodeType::JOIN) {
            pinned.insert(node);
            for (Node *armTail : node->withInputs()) {
                if (armTail) {
                    pinned.insert(armTail);
                }
            }
            for (Node *ctrlIn : node->ctrlInputs()) {
                if (ctrlIn) {
                    pinned.insert(ctrlIn);
                }
            }
            for (Node *normIn : node->normInputs()) {
                if (normIn) {
                    pinned.insert(normIn);
                }
            }
        }
    }
    return pinned;
}

size_t pruneUnreachableSlotSafe(GraphRewriteSession &session, const graph_ptr_t &graph) {
    if (!graph || !graph->hasOutput()) {
        return 0;
    }
    std::unordered_set<Node *> live   = collectLiveNodesFromExit(graph);
    std::unordered_set<Node *> pinned = collectBranchSlotPinnedNodes(graph);

    std::vector<Node *> toErase;
    toErase.reserve(graph->nodes().size());
    for (Node *node : graph->nodes()) {
        if (live.contains(node)) {
            continue;
        }
        // BRCH/JOIN arm 槽位由边序定义语义。若对应节点在 rewrite 中已退化为不可达，
        // 也先保留骨架节点，避免 detach/unlink 对输入输出向量重排而打乱 slot 对齐。
        if (pinned.contains(node)) {
            continue;
        }
        toErase.push_back(node);
    }
    for (Node *node : toErase) {
        session.eraseNode(node);
    }
    return toErase.size();
}

graph_ptr_t ensureEditableGraph(
    GraphRewriteSession &session, const graph_ptr_t &sourceRoot, const graph_ptr_t &sourceGraph,
    std::unordered_map<const Graph *, graph_ptr_t> &cache) {
    if (sourceGraph == sourceRoot) {
        return cache.at(sourceRoot.get());
    }
    auto existing = cache.find(sourceGraph.get());
    if (existing != cache.end()) {
        return existing->second;
    }

    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("InlinePass")
            .debug(
                "Materializing graph {} ({:p}) under source root {} ({:p}).",
                sourceGraph->name(),
                static_cast<const void *>(sourceGraph.get()),
                sourceRoot->name(),
                static_cast<const void *>(sourceRoot.get())));

    graph_ptr_t imported;
    if (auto sourceOwner = sourceGraph->outer()) {
        graph_ptr_t draftOwner = ensureEditableGraph(session, sourceRoot, sourceOwner, cache);
        if (hasSubGraphRef(draftOwner, sourceGraph)) {
            session.eraseSubGraph(draftOwner, sourceGraph);
        }
        if (draftOwner->dependencies().contains(sourceGraph)) {
            session.eraseDependency(draftOwner, sourceGraph);
        }
        imported = session.importSubGraph(draftOwner, sourceGraph, GraphImportMode::CloneIntoDraft);
        if (sourceOwner->dependencies().contains(sourceGraph)) {
            session.importDependency(draftOwner, imported);
        }
    } else {
        graph_ptr_t draftRoot = cache.at(sourceRoot.get());
        if (draftRoot->dependencies().contains(sourceGraph)) {
            session.eraseDependency(draftRoot, sourceGraph);
        }
        imported =
            session.importDependency(draftRoot, sourceGraph, GraphImportMode::CloneIntoDraft);
    }

    ASSERT(
        imported->hasOutput(),
        std::format(
            "Imported draft graph '{}' cloned from '{}' has no output.",
            imported->name(),
            sourceGraph->name()));
    cache[sourceGraph.get()] = imported;
    return imported;
}

} // namespace

graph_ptr_t InlineRewritePass::apply(graph_ptr_t &graph, ostream &os) {
    (void)os;
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("InlinePass")
            .debug("Start inline pass from root graph {}.", graph ? graph->name() : "<null>"));
    if (!graph) {
        return graph;
    }

    GraphRewriteSession session(graph);
    graph_ptr_t sourceRoot  = graph;
    graph_ptr_t workingRoot = session.root();

    std::unordered_map<const Graph *, graph_ptr_t> editableGraphsBySource{
        {sourceRoot.get(), workingRoot}};
    std::vector<graph_ptr_t> sourceGraphs;
    std::unordered_set<const Graph *> visited;
    collectReachableGraphs(sourceRoot, sourceGraphs, visited);

    // 先为整棵图树建立 draft 映射（浅克隆语义下，子图/依赖在此阶段按需 materialize）。
    for (const auto &sourceGraph : sourceGraphs) {
        (void)ensureEditableGraph(session, sourceRoot, sourceGraph, editableGraphsBySource);
    }

    // uses 驱动重定向：把 draft 图里残留的 legacy 图引用统一重写为 draft 映射，并同步 dependency。
    std::vector<graph_ptr_t> draftGraphs;
    draftGraphs.reserve(sourceGraphs.size());
    for (const auto &sourceGraph : sourceGraphs) {
        draftGraphs.push_back(editableGraphsBySource.at(sourceGraph.get()));
    }
    GraphUseIndex useIndex = buildUseIndex(draftGraphs);
    retargetLegacyRefsByUseIndex(session, sourceRoot, useIndex, editableGraphsBySource);
    std::unordered_set<const Graph *> legacySources;
    legacySources.reserve(sourceGraphs.size());
    for (const auto &sourceGraph : sourceGraphs) {
        legacySources.insert(sourceGraph.get());
    }
    assertNoLegacyGraphRefs(draftGraphs, legacySources);

    // SCC 缩点 + 逆拓扑顺序：先处理最深层被引用图，再逐层向上。
    // 这样可以避免“先改上层导致下层反复重扫”的多轮展开抖动。
    SccGraphPlan sccPlan = buildSccGraphPlan(sourceGraphs);
    for (size_t sccId : sccPlan.processOrder) {
        std::vector<InlineTarget> targets =
            collectInitialInlineTargetsForScc(sccId, sccPlan, editableGraphsBySource);
        if (targets.empty()) {
            continue;
        }

        if (targets.size() > kInlineTargetBudgetPerScc) {
            EXEC_WHEN_DEBUG(ASSERT(
                false,
                std::format(
                    "Inline budget exceeded before SCC {} starts: targets={}, limit={}.",
                    sccId,
                    targets.size(),
                    kInlineTargetBudgetPerScc)));
            GetDefaultLogger()
                .in("InlinePass")
                .warn(
                    "Skip SCC {} due to target budget exceeded in release: targets={}, limit={}.",
                    sccId,
                    targets.size(),
                    kInlineTargetBudgetPerScc);
            continue;
        }

        std::unordered_map<const Graph *, size_t> nodeCountBefore;
        for (const Graph *sourceGraph : sccPlan.components[sccId]) {
            graph_ptr_t owner            = editableGraphsBySource.at(sourceGraph);
            nodeCountBefore[sourceGraph] = owner->nodes().size();
        }
        std::vector<graph_ptr_t> changedGraphs;
        size_t appliedCount = 0;
        for (const auto &target : targets) {
            Node *brch = target.branch;
            Node *path = target.path;
            if (!brch || !path || path->type() != NodeType::FUNC) {
                continue;
            }
            if (path->graph().shared_from_this() != target.owner) {
                continue;
            }
            auto *funcPath = tt::as_ptr<FuncNode>(path);
            if (!funcPath->bodyGraph()) {
                continue;
            }
            const auto &pathGraph =
                target.bodyGraph ? target.bodyGraph : funcPath->bodyGraph()->shared_from_this();

            InlineOptions inlineOptions;
            inlineOptions.syncPolicy = InlineSyncPolicy::Force;
            InlineResult inlined     = session.inlineCallable(path, inlineOptions);
            if (!inlined || !inlined.valueExit || !inlined.ctrlEntry) {
                continue;
            }
            EXEC_WHEN_DEBUG(
                GetDefaultLogger()
                    .in("InlinePass")
                    .debug(
                        "Inline result in graph {}: path={}, valueExit={}, ctrlEntry={}, "
                        "ctrlExit={}.",
                        target.owner->name(),
                        path->toString(),
                        inlined.valueExit ? inlined.valueExit->toString() : "<null>",
                        inlined.ctrlEntry ? inlined.ctrlEntry->toString() : "<null>",
                        inlined.ctrlExit ? inlined.ctrlExit->toString() : "<null>"));

            // BRCH/JOIN arm 语义要求“控制路径 + 值产出”整体可达。
            // 不能直接给 DATA 等值节点挂 ctrl 边（会污染调度语义），
            // 因此使用 NRef 作为受控值代理：ctrlExit -> NRef, valueExit -> NRef。
            Node *ctrlAnchor = inlined.ctrlExit ? inlined.ctrlExit : inlined.ctrlEntry;
            if (ctrlAnchor && ctrlAnchor != inlined.valueExit && target.join &&
                target.join->graph().shared_from_this() == target.owner) {
                auto *gatedValue = NRefNode::create(*target.owner);
                gatedValue->setDataType(target.join->dataType());
                session.link(LinkType::Ctrl, ctrlAnchor, gatedValue);
                session.link(LinkType::Norm, inlined.valueExit, gatedValue);
                session.replaceInput(LinkType::With, target.join, inlined.valueExit, gatedValue);
                inlined.valueExit = gatedValue;
                EXEC_WHEN_DEBUG(
                    GetDefaultLogger()
                        .in("InlinePass")
                        .debug(
                            "Inserted NRef gate {} for inlined path {} in graph {}.",
                            gatedValue->toString(),
                            path->toString(),
                            target.owner->name()));
            }

            if (inlined.ctrlEntry == path) {
                continue;
            }
            while (std::ranges::find(brch->ctrlOutputs(), path) != brch->ctrlOutputs().end()) {
                session.replaceOutput(LinkType::Ctrl, brch, path, inlined.ctrlEntry);
            }
            session.eraseNode(path);
            auto sameGraph = [&](const graph_ptr_t &g) { return g.get() == target.owner.get(); };
            if (std::ranges::find_if(changedGraphs, sameGraph) == changedGraphs.end()) {
                changedGraphs.push_back(target.owner);
            }
            appliedCount++;
            if (appliedCount > kInlineApplyBudgetPerScc) {
                EXEC_WHEN_DEBUG(ASSERT(
                    false,
                    std::format(
                        "Inline apply budget exceeded in SCC {}: applied={}, limit={}.",
                        sccId,
                        appliedCount,
                        kInlineApplyBudgetPerScc)));
                GetDefaultLogger()
                    .in("InlinePass")
                    .warn(
                        "Stop applying inline targets in SCC {} due to apply budget exceeded in "
                        "release: applied={}, limit={}.",
                        sccId,
                        appliedCount,
                        kInlineApplyBudgetPerScc);
                break;
            }

            GetDefaultLogger()
                .in("InlinePass")
                .info(
                    "Inlined FUNC node {} (graph {}) between BRCH and JOIN node {} in graph {}.",
                    path->toString(),
                    pathGraph->name(),
                    brch->toString(),
                    target.owner->name());
        }

        size_t nodeDelta = 0;
        for (const Graph *sourceGraph : sccPlan.components[sccId]) {
            graph_ptr_t owner = editableGraphsBySource.at(sourceGraph);
            size_t before     = nodeCountBefore[sourceGraph];
            size_t after      = owner->nodes().size();
            if (after > before) {
                nodeDelta += (after - before);
            }
        }
        EXEC_WHEN_DEBUG(
            GetDefaultLogger()
                .in("InlinePass")
                .debug("Node delta computed for SCC {}: {}.", sccId, nodeDelta));
        if (nodeDelta > kNodeDeltaBudgetPerScc) {
            EXEC_WHEN_DEBUG(ASSERT(
                false,
                std::format(
                    "Inline node-delta budget exceeded in SCC {}: delta={}, limit={}.",
                    sccId,
                    nodeDelta,
                    kNodeDeltaBudgetPerScc)));
            GetDefaultLogger()
                .in("InlinePass")
                .warn(
                    "Node delta budget exceeded in SCC {} (release): delta={}, limit={}.",
                    sccId,
                    nodeDelta,
                    kNodeDeltaBudgetPerScc);
        }

        for (const auto &changedGraph : changedGraphs) {
            ASSERT(
                changedGraph->hasOutput(),
                std::format("Graph '{}' lost output after inline rewrite.", changedGraph->name()));
            const size_t pruned = pruneUnreachableSlotSafe(session, changedGraph);
            EXEC_WHEN_DEBUG(
                GetDefaultLogger()
                    .in("InlinePass")
                    .debug(
                        "Slot-safe prune in graph {} removed {} unreachable nodes.",
                        changedGraph->name(),
                        pruned));
            (void)pruned;
            GraphBuilder::validateGraph(*changedGraph);
        }
    }

    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("InlinePass")
            .debug(
                "Running adjacency ownership checks for {} draft graphs.",
                editableGraphsBySource.size()));
    for (const auto &[_, g] : editableGraphsBySource) {
        assertAdjacencyPointersBelongToGraph(g);
    }
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("InlinePass").debug("Adjacency ownership checks passed."));

    auto result = session.finish();
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("InlinePass")
            .debug(
                "Inline session finished: changed={}, resultRoot={}.",
                result.changed,
                result.graph ? result.graph->name() : "<null>"));
    graph = result.graph;
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("InlinePass")
            .debug("Inline pass apply returns root {}.", graph ? graph->name() : "<null>"));
    return graph;
}
