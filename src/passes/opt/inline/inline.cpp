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
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "inline.h"

#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_session.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCFuncBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::kInvalidNodeRef;

constexpr size_t kRuntimeInlineApplyBudget = 256;

bool shouldInlineTarget(
    bool isSmallSubgraph, bool isBranchArmHead, const InlineRewriteConfig &config) {
    switch (config.inlineStrategy) {
    case InlineTargetStrategy::Small:
        return isSmallSubgraph && !isBranchArmHead;
    case InlineTargetStrategy::Arm:
        return isBranchArmHead;
    case InlineTargetStrategy::Hybrid:
        return isSmallSubgraph || isBranchArmHead;
    default:
        return false;
    }
}

bool isSmallRuntimeSubgraphForInline(const GCGraph *bodyGraph, const InlineRewriteConfig &config) {
    if (!bodyGraph) {
        return false;
    }

    size_t nonDataPortCount = 0;
    for (auto it = bodyGraph->nodes().begin(); it != bodyGraph->nodes().end(); ++it) {
        const auto *node = *it;
        if (!node) {
            continue;
        }
        if (node->kind == GCNodeKind::Data || node->kind == GCNodeKind::Port) {
            continue;
        }
        nonDataPortCount++;
        if (nonDataPortCount > config.smallSubgraphMaxNonDataPortNodes) {
            return false;
        }
    }
    return true;
}

bool isDraftBranchArmHead(const camel::runtime::GraphDraft &draft, gc_node_ref_t nodeId) {
    if (!draft.alive(nodeId)) {
        return false;
    }
    for (gc_node_ref_t pred : draft.ctrlInputsOf(nodeId)) {
        const auto *predHeader = draft.header(pred);
        if (predHeader && predHeader->kind == GCNodeKind::Brch) {
            return true;
        }
    }
    return false;
}

struct RuntimeCallGraphSccInfo {
    std::unordered_map<const GCGraph *, size_t> componentOf;
    std::vector<std::vector<const GCGraph *>> components;
    std::unordered_set<const GCGraph *> recursiveGraphs;
    std::unordered_set<const GCGraph *> componentEntryGraphs;
};

RuntimeCallGraphSccInfo analyzeRuntimeCallGraphScc(const std::vector<GCGraph *> &closure) {
    RuntimeCallGraphSccInfo info;
    std::unordered_map<const GCGraph *, size_t> indexOf;
    indexOf.reserve(closure.size());
    for (size_t i = 0; i < closure.size(); ++i) {
        indexOf.emplace(closure[i], i);
    }

    std::vector<std::vector<size_t>> edges(closure.size());
    std::vector<std::vector<size_t>> reverseEdges(closure.size());
    for (size_t i = 0; i < closure.size(); ++i) {
        GCGraph *graph = closure[i];
        if (!graph) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            GCGraph *callee = graph->directCalleeGraphOf(it.ref());
            if (!callee) {
                continue;
            }
            auto calleeIt = indexOf.find(callee);
            if (calleeIt == indexOf.end()) {
                continue;
            }
            edges[i].push_back(calleeIt->second);
            reverseEdges[calleeIt->second].push_back(i);
        }
    }

    std::vector<size_t> order;
    order.reserve(closure.size());
    std::vector<bool> visited(closure.size(), false);
    std::function<void(size_t)> dfs1 = [&](size_t u) {
        if (visited[u]) {
            return;
        }
        visited[u] = true;
        for (size_t v : edges[u]) {
            dfs1(v);
        }
        order.push_back(u);
    };
    for (size_t i = 0; i < closure.size(); ++i) {
        dfs1(i);
    }

    std::fill(visited.begin(), visited.end(), false);
    std::function<void(size_t, std::vector<size_t> &)> dfs2 = [&](size_t u,
                                                                  std::vector<size_t> &component) {
        if (visited[u]) {
            return;
        }
        visited[u] = true;
        component.push_back(u);
        for (size_t v : reverseEdges[u]) {
            dfs2(v, component);
        }
    };

    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        std::vector<size_t> component;
        dfs2(*it, component);
        if (component.empty()) {
            continue;
        }
        const size_t componentId = info.components.size();
        info.components.emplace_back();
        auto &graphsInComponent = info.components.back();
        graphsInComponent.reserve(component.size());
        for (size_t idx : component) {
            info.componentOf.emplace(closure[idx], componentId);
            graphsInComponent.push_back(closure[idx]);
        }

        bool isRecursive = component.size() > 1;
        if (!isRecursive) {
            const size_t u = component.front();
            isRecursive    = std::find(edges[u].begin(), edges[u].end(), u) != edges[u].end();
        }
        if (isRecursive) {
            for (size_t idx : component) {
                info.recursiveGraphs.insert(closure[idx]);
            }
        }

        // SCC entry graphs are nodes that receive edges from outside the SCC.
        // When inlining recursive components, callers may inline helper graphs
        // inside the SCC, but must not inline the SCC entry back into the
        // component or the graph size will grow without bound.
        for (size_t idx : component) {
            for (size_t pred : reverseEdges[idx]) {
                if (std::find(component.begin(), component.end(), pred) == component.end()) {
                    info.componentEntryGraphs.insert(closure[idx]);
                    break;
                }
            }
        }
    }
    return info;
}

bool shouldSkipRecursiveInline(
    const GCGraph *caller, const GCGraph *callee, const RuntimeCallGraphSccInfo &sccInfo,
    const InlineRewriteConfig &config) {
    if (!caller || !callee) {
        return true;
    }
    if (!sccInfo.recursiveGraphs.contains(callee)) {
        return false;
    }

    const auto callerIt = sccInfo.componentOf.find(caller);
    const auto calleeIt = sccInfo.componentOf.find(callee);
    if (callerIt == sccInfo.componentOf.end() || calleeIt == sccInfo.componentOf.end()) {
        return false;
    }

    if (callerIt->second != calleeIt->second) {
        // External callers must not inline a recursive graph wholesale into
        // their own body. The profitable transformation is to compact helper
        // graphs inside the recursive SCC itself so the recursive entry graph
        // becomes scheduler/JIT-friendly, while keeping the recursive cycle
        // encapsulated inside that graph.
        return true;
    }

    if (config.blockCallsToSccEntryCallees && sccInfo.componentEntryGraphs.contains(callee)) {
        return true;
    }
    return false;
}

bool applyRuntimeInline(
    const camel::core::context::context_ptr_t &context, GCGraph *runtimeRoot,
    const InlineRewriteConfig &config) {
    camel::runtime::RuntimeGraphDraftSession session(context, runtimeRoot);
    bool changed = false;
    CAMEL_LOG_INFO_S("InlinePass", "Runtime inline session started.");
    size_t appliedCount = 0;

    while (true) {
        const std::vector<GCGraph *> closure = session.collectReachableRuntimeGraphs();
        CAMEL_LOG_INFO_S(
            "InlinePass",
            "Runtime inline collected reachable closure with {} graphs.",
            closure.size());
        std::unordered_set<GCGraph *> closureSet(closure.begin(), closure.end());
        for (GCGraph *runtimeGraph : closure) {
            if (!runtimeGraph) {
                continue;
            }
            for (auto it = runtimeGraph->nodes().begin(); it != runtimeGraph->nodes().end(); ++it) {
                const auto *node = *it;
                if (!node || node->kind != GCNodeKind::Func) {
                    continue;
                }
                const auto *body = runtimeGraph->nodeBodyAs<GCFuncBody>(it.ref());
                if (!body || !body->calleeGraph) {
                    continue;
                }
                if (!closureSet.contains(body->calleeGraph)) {
                    throw std::runtime_error(
                        std::format(
                            "Runtime inline discovered FUNC node {} in graph '{}' with callee "
                            "graph {:p} outside the reachable closure.",
                            it.ref(),
                            runtimeGraph->name(),
                            static_cast<void *>(body->calleeGraph)));
                }
            }
        }

        const auto sccInfo = analyzeRuntimeCallGraphScc(closure);
        CAMEL_LOG_INFO_S(
            "InlinePass",
            "Runtime inline SCC analysis finished: {} components, {} recursive graphs, {} entry "
            "graphs.",
            sccInfo.components.size(),
            sccInfo.recursiveGraphs.size(),
            sccInfo.componentEntryGraphs.size());

        bool roundChanged = false;
        for (auto it = closure.rbegin(); it != closure.rend(); ++it) {
            GCGraph *nextGraph = *it;
            if (!nextGraph) {
                continue;
            }

            CAMEL_LOG_INFO_S(
                "InlinePass",
                "Runtime inline scanning graph '{}' for candidates.",
                nextGraph->name());
            camel::runtime::GraphDraft &draft = session.edit(nextGraph);
            std::vector<gc_node_ref_t> funcNodes;
            std::vector<gc_node_ref_t> callNodes;
            funcNodes.reserve(draft.nodeCount());
            callNodes.reserve(draft.nodeCount());

            for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
                const auto *header = draft.header(id);
                if (!header) {
                    continue;
                }
                if (header->kind == GCNodeKind::Func) {
                    funcNodes.push_back(id);
                } else if (header->kind == GCNodeKind::Call) {
                    callNodes.push_back(id);
                }
            }

            if (config.enableSpecialization) {
                for (gc_node_ref_t funcNodeId : funcNodes) {
                    if (!draft.alive(funcNodeId)) {
                        continue;
                    }
                    if (camel::runtime::specializeDirectFuncInDraft(draft, funcNodeId)) {
                        changed      = true;
                        roundChanged = true;
                    }
                }
            }

            if (config.enableDevirtualization) {
                for (gc_node_ref_t callNodeId : callNodes) {
                    if (!draft.alive(callNodeId)) {
                        continue;
                    }
                    if (camel::runtime::devirtualizeStaticCallInDraft(draft, callNodeId)) {
                        changed      = true;
                        roundChanged = true;
                    }
                }
            }

            std::vector<gc_node_ref_t> candidates;
            candidates.reserve(draft.nodeCount());
            for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
                const auto *header = draft.header(id);
                if (!header || header->kind != GCNodeKind::Func) {
                    continue;
                }
                if (draft.sourceRefOf(id) == kInvalidNodeRef) {
                    continue;
                }
                const auto payload = draft.payloadOf(id);
                if (payload.size_bytes() < sizeof(GCFuncBody)) {
                    continue;
                }
                const auto *body = reinterpret_cast<const GCFuncBody *>(payload.data());
                if (!body->calleeGraph || body->calleeGraph == nextGraph) {
                    continue;
                }
                if (shouldSkipRecursiveInline(nextGraph, body->calleeGraph, sccInfo, config)) {
                    continue;
                }

                const bool isSmall = isSmallRuntimeSubgraphForInline(body->calleeGraph, config);
                const bool isArm   = isDraftBranchArmHead(draft, id);
                if (shouldInlineTarget(isSmall, isArm, config)) {
                    candidates.push_back(id);
                }
            }

            CAMEL_LOG_INFO_S(
                "InlinePass",
                "Graph '{}' collected {} inline candidates.",
                nextGraph->name(),
                candidates.size());

            for (gc_node_ref_t funcNodeId : candidates) {
                if (!draft.alive(funcNodeId)) {
                    continue;
                }
                const auto inlined =
                    camel::runtime::inlineCallableInDraft(session, draft, funcNodeId);
                if (!inlined) {
                    continue;
                }
                changed      = true;
                roundChanged = true;
                appliedCount++;
                if (appliedCount >= kRuntimeInlineApplyBudget) {
                    CAMEL_LOG_WARN_S(
                        "InlinePass",
                        "Runtime-native inline stops at apply budget {}.",
                        kRuntimeInlineApplyBudget);
                    break;
                }
            }
            if (appliedCount >= kRuntimeInlineApplyBudget) {
                break;
            }
        }

        if (appliedCount >= kRuntimeInlineApplyBudget) {
            break;
        }
        if (!roundChanged) {
            break;
        }
    }

    if (!changed) {
        return false;
    }

    (void)session.commit();
    return true;
}

} // namespace

camel::runtime::GCGraph *
InlineRewritePass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    (void)os;
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "InlinePass",
        "Start runtime-native inline pass from root {}.",
        graph ? graph->name() : "<null>"));

    if (!graph) {
        return nullptr;
    }

    if (!config_.enableInlining) {
        return graph;
    }

    if (!applyRuntimeInline(context_, graph, config_)) {
        return graph;
    }

    camel::runtime::GCGraph *rewrittenRoot = context_->runtimeRootGraph();
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "InlinePass",
        "Runtime-native inline committed rewritten root {}.",
        rewrittenRoot ? rewrittenRoot->name() : "<null>"));
    return rewrittenRoot;
}
