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
 * Updated: Apr. 10, 2026
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

bool isRuntimeBranchArmHead(const GCGraph *owner, gc_node_ref_t nodeRef) {
    if (!owner || nodeRef == kInvalidNodeRef) {
        return false;
    }
    for (gc_node_ref_t pred : owner->ctrlInputsOf(nodeRef)) {
        const auto *predNode = owner->node(pred);
        if (predNode && predNode->kind == GCNodeKind::Brch) {
            return true;
        }
    }
    return false;
}

std::unordered_set<const GCGraph *>
collectRecursiveRuntimeGraphs(const std::vector<GCGraph *> &closure) {
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

    std::unordered_set<const GCGraph *> recursive;
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
        if (component.size() > 1) {
            for (size_t idx : component) {
                recursive.insert(closure[idx]);
            }
            continue;
        }

        const size_t u = component.front();
        if (std::find(edges[u].begin(), edges[u].end(), u) != edges[u].end()) {
            recursive.insert(closure[u]);
        }
    }
    return recursive;
}

bool applyRuntimeInline(
    const camel::core::context::context_ptr_t &context, GCGraph *runtimeRoot,
    const InlineRewriteConfig &config) {
    camel::runtime::RuntimeGraphDraftSession session(context, runtimeRoot);
    bool changed = false;

    while (true) {
        bool iterationChanged                = false;
        const std::vector<GCGraph *> closure = session.collectReachableRuntimeGraphs();
        const auto recursiveGraphs           = collectRecursiveRuntimeGraphs(closure);
        size_t appliedCount                  = 0;

        for (GCGraph *runtimeGraph : closure) {
            if (!runtimeGraph) {
                continue;
            }
            camel::runtime::GraphDraft &draft = session.edit(runtimeGraph);
            std::vector<gc_node_ref_t> candidates;
            candidates.reserve(draft.nodeCount());

            for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
                const auto *header = draft.header(id);
                if (!header || header->kind != GCNodeKind::Func) {
                    continue;
                }
                const auto payload = draft.payloadOf(id);
                if (payload.size_bytes() < sizeof(GCFuncBody)) {
                    continue;
                }
                const auto *body = reinterpret_cast<const GCFuncBody *>(payload.data());
                if (!body->calleeGraph || body->calleeGraph == runtimeGraph) {
                    continue;
                }
                if (runtimeGraph->name().starts_with("__") ||
                    body->calleeGraph->name().starts_with("__")) {
                    continue;
                }
                if (recursiveGraphs.contains(body->calleeGraph)) {
                    continue;
                }

                const bool isSmall = isSmallRuntimeSubgraphForInline(body->calleeGraph, config);
                const bool isArm   = isRuntimeBranchArmHead(runtimeGraph, id);
                if (shouldInlineTarget(isSmall, isArm, config)) {
                    candidates.push_back(id);
                }
            }

            for (gc_node_ref_t funcNodeId : candidates) {
                if (!draft.alive(funcNodeId)) {
                    continue;
                }
                const auto inlined = camel::runtime::inlineCallableInDraft(draft, funcNodeId);
                if (!inlined) {
                    continue;
                }
                iterationChanged = true;
                changed          = true;
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

        if (!iterationChanged) {
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
