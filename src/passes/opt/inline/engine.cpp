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
 * Created: Apr. 10, 2026
 * Updated: Oct. 03, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Shared runtime rewrite engine for GCGraph-native optimization passes.
 *
 * This file hosts the actual multi-phase rewrite loop used by:
 * - `std::inline`        : inline only
 * - `std::devirtualize`  : static-call devirtualization only
 * - `std::specialize`    : direct-call specialization only
 * - `std::opt`           : aggregate optimization pipeline
 *
 * Keeping the execution engine here avoids smuggling extra optimization phases
 * behind the `InlineRewritePass` wrapper and makes the pass surface explicit.
 */

#include "engine.h"

#include "../generic/rewrite.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_session.h"
#include "camel/utils/log.h"

#include <optional>
#include <algorithm>
#include <cstdlib>
#include <format>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCFuncBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::kInvalidNodeRef;

constexpr size_t kRuntimeRewriteApplyBudget = 256;

bool hasAnyRewritePhaseEnabled(const OptimizeRewriteConfig &config) {
    return config.enableInlining || config.enableDevirtualization || config.enableSpecialization;
}

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

/// Nodes of `graph` that inlining copies (all but constants and ports).
size_t inlinedNodeCount(const GCGraph *graph) {
    size_t count = 0;
    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const auto *node = *it;
        count += node && node->kind != GCNodeKind::Data && node->kind != GCNodeKind::Port ? 1 : 0;
    }
    return count;
}

/// True when values of `type` may hold a function: inlining a callee returning one exposes the
/// closure it builds, so that calls of it can be devirtualized (autodiff's (value, pullback)).
bool mayHoldFunction(const camel::core::type::Type *type) {
    using camel::core::type::TypeCode;
    if (!type) {
        return false;
    }
    if (type->code() == TypeCode::Function) {
        return true;
    }
    if (type->code() == TypeCode::Tuple) {
        for (auto *element : static_cast<const camel::core::type::TupleType *>(type)->types()) {
            if (mayHoldFunction(element)) {
                return true;
            }
        }
    }
    return false;
}

bool returnsClosure(const GCGraph *graph) {
    const auto *type = graph ? graph->funcType() : nullptr;
    return type && type->hasExitType() && mayHoldFunction(type->exitType());
}

/// Number of direct calls of each graph in `closure`, read from the session's drafts where a
/// graph is being edited (calls of graphs created in this session exist only there).
std::unordered_map<const GCGraph *, size_t> directCallCounts(
    const camel::runtime::RuntimeGraphDraftSession &session,
    const std::vector<GCGraph *> &closure) {
    std::unordered_map<const GCGraph *, size_t> counts;
    for (GCGraph *graph : closure) {
        if (const auto *draft = session.tryDraft(graph)) {
            for (gc_node_ref_t id = 0; id < draft->nodeSlotCount(); ++id) {
                const auto *header = draft->header(id);
                if (!header || header->kind != GCNodeKind::Func) {
                    continue;
                }
                const auto payload = draft->payloadOf(id);
                if (payload.size_bytes() >= sizeof(GCFuncBody)) {
                    const auto *body = reinterpret_cast<const GCFuncBody *>(payload.data());
                    if (body->calleeGraph) {
                        ++counts[body->calleeGraph];
                    }
                }
            }
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if (GCGraph *callee = graph->directCalleeGraphOf(it.ref())) {
                ++counts[callee];
            }
        }
    }
    return counts;
}

/// The nodes `id` runs after other than through `skip` (its transitive inputs of every kind).
std::vector<bool>
upstreamOf(const camel::runtime::GraphDraft &draft, gc_node_ref_t id, gc_node_ref_t skip) {
    std::vector<bool> seen(draft.nodeSlotCount(), false);
    std::vector<gc_node_ref_t> work;
    for (auto inputs : {draft.normInputsOf(id), draft.withInputsOf(id), draft.ctrlInputsOf(id)}) {
        work.insert(work.end(), inputs.begin(), inputs.end());
    }
    while (!work.empty()) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (n >= seen.size() || seen[n] || n == skip) {
            continue;
        }
        seen[n] = true;
        for (auto inputs : {draft.normInputsOf(n), draft.withInputsOf(n), draft.ctrlInputsOf(n)}) {
            work.insert(work.end(), inputs.begin(), inputs.end());
        }
    }
    return seen;
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

/// The graphs a macro operator that is still in the program will transform.
///
/// A macro operator over a function value (`grad(f)`) rewrites f's graph. `std::macro` expands
/// it ahead of time; when it has not run (or could not evaluate the call), the operator runs
/// with the program and transforms whatever graph the rewrite passes left. Such transforms are
/// written against the graph as compiled. The pullback builder of `autodiff` takes one pullback
/// per branch-arm call, so it rejects an arm that was merged into its owner, and it crashes on
/// the shape recursion compaction leaves (the recursive call inlined into the arm, which makes
/// the arm's graph an arm of itself). So nothing is inlined into the graphs an unexpanded macro
/// operator can still reach: its function operand, and what that calls or holds as a function
/// constant, including the copies specialization made of them earlier in the same session. Calls
/// of these graphs from elsewhere are inlined as usual (the copy belongs to the caller).
struct MacroOperandGraphs {
    std::unordered_set<const GCGraph *> graphs;
    /// A macro operator takes a function that is not a constant of the graph (a parameter, the
    /// result of a call): any function of the program may reach it.
    bool all = false;

    bool contains(const GCGraph *graph) const { return all || graphs.contains(graph); }
};

/// The graph of the function constant `ref` of `graph` (a DATA node, or the closure template a
/// FILL builds on), or null.
GCGraph *functionConstantGraph(const GCGraph *graph, gc_node_ref_t ref) {
    using camel::core::type::TypeCode;
    const auto *node = graph->node(ref);
    if (node->kind == GCNodeKind::Fill && !graph->normInputsOf(ref).empty()) {
        ref  = graph->normInputsOf(ref).front();
        node = graph->node(ref);
    }
    if (node->kind != GCNodeKind::Data || node->dataIndex >= 0 || !node->dataType ||
        node->dataType->code() != TypeCode::Function) {
        return nullptr;
    }
    auto *function = camel::core::rtdata::fromSlot<::Function *>(
        graph->staticArea()->get<slot_t>(static_cast<size_t>(-node->dataIndex)));
    return function ? function->graph() : nullptr;
}

MacroOperandGraphs collectMacroOperandGraphs(
    const camel::runtime::RuntimeGraphDraftSession &session,
    const std::vector<GCGraph *> &closure) {
    using camel::core::type::TypeCode;
    MacroOperandGraphs result;
    std::vector<const GCGraph *> work;
    for (GCGraph *graph : closure) {
        if (!graph) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto *node = *it;
            if (!node || node->kind != GCNodeKind::Oper || !node->isMacro()) {
                continue;
            }
            for (auto inputs : {graph->normInputsOf(it.ref()), graph->withInputsOf(it.ref())}) {
                for (gc_node_ref_t input : inputs) {
                    const auto *operand = graph->node(input);
                    if (!operand->dataType || operand->dataType->code() != TypeCode::Function) {
                        continue;
                    }
                    if (operand->kind == GCNodeKind::Oper && operand->isMacro()) {
                        continue; // the result of another macro operator, handled at that one
                    }
                    GCGraph *target = functionConstantGraph(graph, input);
                    if (!target) {
                        result.all = true;
                        return result;
                    }
                    if (result.graphs.insert(target).second) {
                        work.push_back(target);
                    }
                }
            }
        }
    }
    const auto reach = [&](GCGraph *target) {
        if (target && result.graphs.insert(target).second) {
            work.push_back(target);
        }
    };
    while (!work.empty()) {
        const GCGraph *graph = work.back();
        work.pop_back();
        // A graph this session already edited calls what its draft calls: a specialization
        // made in this session is a new graph that only the draft refers to, and it is as much
        // part of what the operator transforms as the function it was copied from.
        if (const auto *draft = session.tryDraft(graph)) {
            for (gc_node_ref_t id = 0; id < draft->nodeSlotCount(); ++id) {
                const auto *header = draft->header(id);
                if (!header || header->kind != GCNodeKind::Func) {
                    continue;
                }
                const auto payload = draft->payloadOf(id);
                if (payload.size_bytes() >= sizeof(GCFuncBody)) {
                    reach(reinterpret_cast<const GCFuncBody *>(payload.data())->calleeGraph);
                }
            }
            const auto slots = draft->staticSlots();
            const auto types = draft->staticSlotTypes();
            for (size_t i = 1; i < slots.size() && i < types.size(); ++i) {
                if (types[i] && types[i]->code() == TypeCode::Function) {
                    auto *function = camel::core::rtdata::fromSlot<::Function *>(slots[i]);
                    reach(function ? function->graph() : nullptr);
                }
            }
        }
        if (!graph->hasNodePayload()) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if (!*it) {
                continue;
            }
            GCGraph *target = graph->directCalleeGraphOf(it.ref());
            if (!target) {
                target = functionConstantGraph(graph, it.ref());
            }
            reach(target);
        }
    }
    return result;
}

struct RuntimeCallGraphSccInfo {
    std::unordered_map<const GCGraph *, size_t> componentOf;
    std::vector<std::vector<const GCGraph *>> components;
    std::unordered_set<const GCGraph *> recursiveGraphs;
    std::unordered_set<const GCGraph *> componentEntryGraphs;
    std::unordered_set<const GCGraph *> externallyCalledGraphs;
    // Graphs from which a recursive graph is reachable (recursive graphs included).
    std::unordered_set<const GCGraph *> reachesRecursion;
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
            if (!reverseEdges[idx].empty()) {
                info.externallyCalledGraphs.insert(closure[idx]);
            }
            for (size_t pred : reverseEdges[idx]) {
                if (std::find(component.begin(), component.end(), pred) == component.end()) {
                    info.componentEntryGraphs.insert(closure[idx]);
                    break;
                }
            }
        }
    }

    std::vector<size_t> work;
    for (size_t i = 0; i < closure.size(); ++i) {
        if (info.recursiveGraphs.contains(closure[i])) {
            info.reachesRecursion.insert(closure[i]);
            work.push_back(i);
        }
    }
    while (!work.empty()) {
        const size_t v = work.back();
        work.pop_back();
        for (size_t u : reverseEdges[v]) {
            if (info.reachesRecursion.insert(closure[u]).second) {
                work.push_back(u);
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

/// The rewritten root, or nullptr when nothing changed. `growthSpent` counts the nodes copied
/// so far against `growthBudget` (see OptimizeRewriteConfig::growthBudgetNodes).
GCGraph *applyRuntimeOptimizeRewrite(
    const camel::core::context::context_ptr_t &context, GCGraph *runtimeRoot,
    const OptimizeRewriteConfig &config, size_t growthBudget, size_t &growthSpent) {
    camel::runtime::RuntimeGraphDraftSession session(context, runtimeRoot);
    bool changed              = false;
    size_t appliedInlineCount = 0;
    CAMEL_LOG_INFO_S("OptimizePass", "Runtime rewrite session started.");

    while (true) {
        const std::vector<GCGraph *> closure = session.collectReachableRuntimeGraphs();
        CAMEL_LOG_INFO_S(
            "OptimizePass",
            "Runtime rewrite collected reachable closure with {} graphs.",
            closure.size());

        std::unordered_set<GCGraph *> closureSet;
        RuntimeCallGraphSccInfo sccInfo;
        MacroOperandGraphs macroOperands;
        if (config.enableInlining) {
            macroOperands = collectMacroOperandGraphs(session, closure);
            closureSet.insert(closure.begin(), closure.end());
            for (GCGraph *runtimeGraph : closure) {
                if (!runtimeGraph) {
                    continue;
                }
                for (auto it = runtimeGraph->nodes().begin(); it != runtimeGraph->nodes().end();
                     ++it) {
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
                                "Runtime rewrite discovered FUNC node {} in graph '{}' with callee "
                                "graph {:p} outside the reachable closure.",
                                it.ref(),
                                runtimeGraph->name(),
                                static_cast<void *>(body->calleeGraph)));
                    }
                }
            }

            sccInfo = analyzeRuntimeCallGraphScc(closure);
            CAMEL_LOG_INFO_S(
                "OptimizePass",
                "Runtime rewrite SCC analysis finished: {} components, {} recursive graphs, {} "
                "entry graphs.",
                sccInfo.components.size(),
                sccInfo.recursiveGraphs.size(),
                sccInfo.componentEntryGraphs.size());
        }

        bool roundChanged = false;
        bool restartRound = false;
        // The direct-call counts of the whole closure, shared by the graphs of this round. They
        // depend only on the drafts, which a round changes in three ways: a specialization or a
        // devirtualization retargets or creates a FUNC node (the counts are dropped and
        // recomputed on the next use), and an inline restarts the round. Counting the closure
        // again for every graph made one inline cost (graphs x nodes): 405 s for std::inline on
        // a 36,000-node program of 1,732 graphs.
        std::optional<std::unordered_map<const GCGraph *, size_t>> roundCallCounts;
        for (auto it = closure.rbegin(); it != closure.rend(); ++it) {
            GCGraph *nextGraph = *it;
            if (!nextGraph) {
                continue;
            }

            CAMEL_LOG_INFO_S(
                "OptimizePass",
                "Runtime rewrite scanning graph '{}' for enabled phases.",
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
                    if (growthSpent >= growthBudget) {
                        break; // out of growth budget: a specialization is a copy of the callee
                    }
                    if (camel::runtime::specializeDirectFuncInDraft(session, draft, funcNodeId)) {
                        changed      = true;
                        roundChanged = true;
                        roundCallCounts.reset();
                        // The callee after the rewrite is the copy (counted in full even when
                        // an existing copy was reused).
                        const auto *body =
                            reinterpret_cast<const GCFuncBody *>(draft.payloadOf(funcNodeId).data());
                        growthSpent += body->calleeGraph ? body->calleeGraph->nodeCount() : 0;
                    }
                }
            }

            if (config.enableDevirtualization) {
                for (gc_node_ref_t callNodeId : callNodes) {
                    if (!draft.alive(callNodeId)) {
                        continue;
                    }
                    // A callee projected out of a branch JOIN needs the branch split first;
                    // the plain static devirtualization handles the rest. The split shape is
                    // export-only (see config), so it is attempted before, not instead of, the
                    // VM-safe static path only when explicitly enabled.
                    if ((config.enableBranchedCallSplit &&
                         camel::runtime::devirtualizeBranchedCallInDraft(
                             session,
                             draft,
                             callNodeId)) ||
                        camel::runtime::devirtualizeStaticCallInDraft(
                            session,
                            draft,
                            callNodeId,
                            config.enableBranchedCallSplit)) {
                        changed      = true;
                        roundChanged = true;
                        roundCallCounts.reset();
                    }
                }
            }

            if (!config.enableInlining) {
                continue;
            }
            // An unexpanded macro operator will transform this graph and needs it as compiled
            // (see MacroOperandGraphs): nothing is inlined into it.
            if (macroOperands.contains(nextGraph)) {
                continue;
            }

            if (!roundCallCounts) {
                roundCallCounts = directCallCounts(session, closure);
            }
            const auto &callCounts = *roundCallCounts;
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
                if (shouldSkipRecursiveInline(
                        nextGraph,
                        body->calleeGraph,
                        sccInfo,
                        config.inlineConfig)) {
                    continue;
                }
                // Growth: the copy must fit the pass's budget and the owner's size cap.
                const size_t copied = inlinedNodeCount(body->calleeGraph);
                if (growthSpent + copied > growthBudget ||
                    draft.nodeCount() + copied > config.inlineConfig.maxOwnerNodes) {
                    continue;
                }

                // Beyond small graphs, inlining a callee that reaches no recursion pays when it
                // cannot duplicate code (a graph with one call site) or duplicates little (see
                // duplicationBudgetNodes), or when it exposes a closure the callee builds, so
                // that calls of it can be devirtualized.
                // Graphs created in this session (specializations, lifted closures) are not in
                // the call-graph analysis yet; they qualify in the next session.
                const bool acyclic = sccInfo.componentOf.contains(body->calleeGraph) &&
                                     !sccInfo.reachesRecursion.contains(body->calleeGraph);
                const size_t calls =
                    callCounts.contains(body->calleeGraph) ? callCounts.at(body->calleeGraph) : 0;
                const bool isSmall =
                    isSmallRuntimeSubgraphForInline(body->calleeGraph, config.inlineConfig) ||
                    (acyclic && calls == 1) || (acyclic && returnsClosure(body->calleeGraph)) ||
                    (acyclic && calls > 1 &&
                     (calls - 1) * inlinedNodeCount(body->calleeGraph) <=
                         config.inlineConfig.duplicationBudgetNodes);
                CAMEL_LOG_INFO_S(
                    "OptimizePass",
                    "Inline probe '{}' -> '{}': small={} acyclic={} calls={} closure={}.",
                    nextGraph->name(),
                    body->calleeGraph->name(),
                    isSmallRuntimeSubgraphForInline(body->calleeGraph, config.inlineConfig),
                    acyclic,
                    callCounts.contains(body->calleeGraph) ? callCounts.at(body->calleeGraph) : 0,
                    returnsClosure(body->calleeGraph));
                const bool isArm = isDraftBranchArmHead(draft, id);
                const bool suppressNonRecursiveArmInlining =
                    config.inlineConfig.inlineStrategy == InlineTargetStrategy::Hybrid &&
                    isSmallRuntimeSubgraphForInline(nextGraph, config.inlineConfig) &&
                    sccInfo.externallyCalledGraphs.contains(nextGraph) &&
                    !sccInfo.recursiveGraphs.contains(nextGraph);
                if (suppressNonRecursiveArmInlining && isArm) {
                    continue;
                }
                if (shouldInlineTarget(isSmall, isArm, config.inlineConfig)) {
                    candidates.push_back(id);
                }
            }

            CAMEL_LOG_INFO_S(
                "OptimizePass",
                "Graph '{}' collected {} inline candidates.",
                nextGraph->name(),
                candidates.size());

            for (gc_node_ref_t funcNodeId : candidates) {
                if (!draft.alive(funcNodeId)) {
                    continue;
                }
                // The call's users ran after it, so after what its arguments were computed
                // from (an effect before a statement feeding an argument). The inlined body may
                // not read every argument, so they wait on those explicitly.
                // Only sources a user reaches through the call alone need the explicit edge.
                // The same holds for the call's own control predecessors (a write the call was
                // ordered after): when the inlined body neither reads the argument that carried
                // that ordering nor has an effect of its own to hang it on (a recursion's base
                // case, a constant result), nothing else keeps the predecessor reachable, and a
                // write the program made was silently dropped.
                auto argSources = camel::passes::generic::orderedSources(draft, funcNodeId);
                for (gc_node_ref_t pred : draft.ctrlInputsOf(funcNodeId)) {
                    if (std::ranges::find(argSources, pred) == argSources.end()) {
                        argSources.push_back(pred);
                    }
                }
                std::vector<std::pair<gc_node_ref_t, std::vector<gc_node_ref_t>>> callUsers;
                if (!argSources.empty()) {
                    for (auto group :
                         {draft.normUsersOf(funcNodeId),
                          draft.withUsersOf(funcNodeId),
                          draft.ctrlUsersOf(funcNodeId)}) {
                        for (gc_node_ref_t user : group) {
                            if (std::ranges::find(
                                    callUsers,
                                    user,
                                    &decltype(callUsers)::value_type::first) != callUsers.end()) {
                                continue;
                            }
                            const auto before = upstreamOf(draft, user, funcNodeId);
                            std::vector<gc_node_ref_t> missing;
                            for (gc_node_ref_t source : argSources) {
                                if (source != user && !before[source]) {
                                    missing.push_back(source);
                                }
                            }
                            callUsers.emplace_back(user, std::move(missing));
                        }
                    }
                }
                const size_t copied = inlinedNodeCount(
                    reinterpret_cast<const GCFuncBody *>(draft.payloadOf(funcNodeId).data())
                        ->calleeGraph);
                const auto inlineResult =
                    camel::runtime::inlineCallableInDraft(session, draft, funcNodeId);
                if (!inlineResult) {
                    continue;
                }
                growthSpent += copied;
                for (const auto &[user, missing] : callUsers) {
                    if (!draft.alive(user)) {
                        continue;
                    }
                    for (gc_node_ref_t source : missing) {
                        const auto existing = draft.ctrlInputsOf(user);
                        if (draft.alive(source) &&
                            std::ranges::find(existing, source) == existing.end()) {
                            draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, user, source);
                        }
                    }
                }
                changed      = true;
                roundChanged = true;
                appliedInlineCount++;
                restartRound = true;
                // Re-scan the reachable closure after every successful inline.
                // Hybrid inline changes control/value context for the remaining
                // FUNC nodes in the same owner graph, so a precomputed
                // candidate list can become stale within the same round.
                break;
            }
            if (restartRound) {
                break;
            }
            if (appliedInlineCount >= kRuntimeRewriteApplyBudget) {
                CAMEL_LOG_WARN_S(
                    "OptimizePass",
                    "Runtime rewrite stops at inline apply budget {}.",
                    kRuntimeRewriteApplyBudget);
                break;
            }
        }

        if (appliedInlineCount >= kRuntimeRewriteApplyBudget) {
            break;
        }
        if (!roundChanged) {
            break;
        }
    }

    if (!changed) {
        return nullptr;
    }
    return session.commit();
}

} // namespace

size_t optimizeGrowthBudget(const OptimizeRewriteConfig &config) {
    if (const char *text = std::getenv(kGrowthBudgetEnvVar); text && *text) {
        char *end                      = nullptr;
        const unsigned long long value = std::strtoull(text, &end, 10);
        if (end && *end == 0) {
            return static_cast<size_t>(value);
        }
    }
    return config.growthBudgetNodes;
}

camel::runtime::GCGraph *applyOptimizeRewritePass(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *graph,
    std::ostream &os, const OptimizeRewriteConfig &config, bool *changedOut) {
    (void)os;

    if (!graph) {
        return nullptr;
    }
    if (!hasAnyRewritePhaseEnabled(config)) {
        return graph;
    }
    const size_t growthBudget = optimizeGrowthBudget(config);
    size_t ownGrowthSpent     = 0;
    size_t &growthSpent       = config.growthSpentNodes ? *config.growthSpentNodes : ownGrowthSpent;
    const bool hadBudget      = growthSpent < growthBudget;
    // A session only inlines calls that existed when it started; calls exposed by its own
    // inlining (nested helpers, devirtualized pullbacks) wait for the next session. Repeat until
    // nothing changes; the bound only guards against a rewrite that never settles.
    constexpr size_t kMaxSessions = 32;
    bool changed                  = false;
    for (size_t i = 0; i < kMaxSessions; ++i) {
        GCGraph *rewritten =
            applyRuntimeOptimizeRewrite(context, graph, config, growthBudget, growthSpent);
        if (!rewritten) {
            break;
        }
        changed = true;
        graph   = rewritten;
    }
    if (hadBudget && growthSpent >= growthBudget) {
        CAMEL_LOG_WARN_S(
            "OptimizePass",
            "Growth budget of {} nodes reached ({} overrides it): the remaining calls are "
            "neither specialized nor inlined.",
            growthBudget,
            kGrowthBudgetEnvVar);
    }
    if (changedOut) {
        *changedOut = changed;
    }
    return graph;
}
