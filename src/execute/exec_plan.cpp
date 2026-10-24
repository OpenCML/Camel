// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Computation of the execution plan of a graph version (camel/runtime/exec_plan.h).
 */

#include "camel/runtime/exec_plan.h"

#include "camel/core/type/composite/tuple.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/control_regions.h"

#include <algorithm>
#include <optional>

namespace camel::runtime {

namespace {

gc_data_idx_t dataIndexOf(const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    ASSERT(node != nullptr, "Execution plan node lookup resolved to null.");
    return node->dataIndex;
}

/// The two slots of the pair of integers that is the sequence of a loop that iterates an
/// interval (`camel::core::LoopIterableForm::Interval`): the tuple FILL the front end built for
/// the two bounds of `for i in a..b` (GCGraph::intervalBoundsOf, which orders the two bounds by
/// the FILL's payload). Nothing when `ref` is not such a pair.
std::optional<std::pair<gc_data_idx_t, gc_data_idx_t>> boundSlotsOf(
    const GCGraph &graph, gc_node_ref_t ref) {
    const auto bounds = graph.intervalBoundsOf(ref);
    if (!bounds.has_value()) {
        return std::nullopt;
    }
    return std::pair{dataIndexOf(graph, bounds->first), dataIndexOf(graph, bounds->second)};
}

} // namespace

const std::vector<ExecNode> &ExecPlan::buildNodes() const {
    std::lock_guard lock(nodesMutex_);
    if (!nodesReady_.load(std::memory_order_relaxed)) {
        const size_t count = topo.size();
        nodes_.resize(count);
        for (size_t idx = 0; idx < count; ++idx) {
            const gc_node_ref_t ref = topo[idx];
            ExecNode &en            = nodes_[idx];
            en.ref                  = ref;
            en.node                 = graph->node(ref);
            ASSERT(en.node != nullptr, "Execution plan node lookup resolved to null.");
            const auto norm = graph->normInputsOf(ref);
            const auto with = graph->withInputsOf(ref);
            en.norm         = norm.data();
            en.normCount    = static_cast<uint32_t>(norm.size());
            en.with         = with.data();
            en.withCount    = static_cast<uint32_t>(with.size());
            if (en.node->hasPayload()) {
                en.body = graph->nodePayloadBytes(ref).data();
            }
            if (en.node->kind == GCNodeKind::Brch) {
                en.armCount = graph->armCountOf(ref);
            }
        }
        nodesReady_.store(true, std::memory_order_release);
    }
    return nodes_;
}

namespace {

/// The value behind the GATEs that order it after statements.
gc_node_ref_t valueBehindGates(const GCGraph &graph, gc_node_ref_t at) {
    for (int guard = 0; guard < 64; ++guard) {
        const GCNode *node = graph.node(at);
        if (node == nullptr || node->kind != GCNodeKind::Gate || graph.normInputsOf(at).empty()) {
            break;
        }
        at = graph.normInputsOf(at).front();
    }
    return at;
}

/// Whether the value of `node` is read only by the node at `reader` (one reference, the reader's):
/// a value nothing else reads can be left unmade.
bool readOnlyBy(
    const GCGraph &graph, std::span<const gc_node_ref_t> live, gc_node_ref_t node,
    gc_node_ref_t reader) {
    size_t seen = 0;
    for (gc_node_ref_t at : live) {
        for (gc_node_ref_t input : graph.normInputsOf(at)) {
            if (input == node && ++seen > 1) {
                return false;
            }
        }
        for (gc_node_ref_t input : graph.withInputsOf(at)) {
            if (input == node && ++seen > 1) {
                return false;
            }
        }
    }
    // The one reference is the reader's own with input (the region's result).
    for (gc_node_ref_t input : graph.withInputsOf(reader)) {
        if (input == node) {
            return true;
        }
    }
    return false;
}

/// Plans a loop of several values of state as its k components (ExecLoop::stateComponents): the
/// body ends in a tuple FILL whose only reader is the LOOP node, and every component is a value a
/// node of the body writes. Then the FILL is not run (plan.elidedAt, whose index `elidedTopo`
/// is) and the state lives in the body's own slots: the tuple that is the loop's value was made
/// once, when the loop started.
///
/// The question this asks is about the *body*: is the tuple it returns an artefact of the state
/// being several values (design principles 3.10) or a value in its own right? Only when nothing
/// but the loop reads it, and every component is computed inside the body (not a port and not a
/// captured value: the state is written into those slots every round), is it the former.
bool planStateComponents(
    const GCGraph &graph, const ControlRegions &regions, ExecPlan &plan, uint32_t bodyRegion,
    gc_node_ref_t loopRef, ExecLoop &loop, size_t &elidedTopo) {
    if (loop.collectionCount != 0 || loop.stateCount < 2 || loop.valueType == nullptr) {
        return false;
    }
    const auto results = graph.withInputsOf(loopRef);
    ASSERT(!results.empty(), "A LOOP node has the result of its body region.");
    const gc_node_ref_t bodyValue = valueBehindGates(graph, results.front());
    const GCNode *fill            = graph.node(bodyValue);
    if (fill == nullptr || fill->kind != GCNodeKind::Fill) {
        return false;
    }
    const camel::core::type::Type *fillType = fill->dataType();
    if (fillType == nullptr || fillType->code() != camel::core::type::TypeCode::Tuple) {
        return false;
    }
    const auto *fillTupleType = static_cast<const camel::core::type::TupleType *>(fillType);
    if (fillTupleType->size() != loop.stateCount) {
        return false;
    }
    for (size_t j = 0; j < loop.stateCount; ++j) {
        const camel::core::type::Type *component = fillTupleType->typeAt(j);
        if (component == nullptr || component != loop.valueType->typeAt(j)) {
            return false;
        }
    }
    // The template of the FILL is a constant: the tuple is made from it, so it holds no value of
    // the body's.
    const auto norm      = graph.normInputsOf(bodyValue);
    const GCNode *source = norm.empty() ? nullptr : graph.node(norm.front());
    if (source == nullptr || source->kind != GCNodeKind::Data) {
        return false;
    }
    const auto *fillBody = graph.nodeBodyAs<camel::runtime::GCFillBody>(bodyValue);
    if (fillBody == nullptr || fillBody->slots().size() != loop.stateCount) {
        return false;
    }
    if (!readOnlyBy(graph, plan.topo, bodyValue, loopRef)) {
        return false;
    }
    // Every component is a value the body computes in a slot of its own: not a port (the state is
    // written into the ports every round) and not a constant of the static area.
    std::vector<gc_data_idx_t> stateSlots(loop.stateCount, 0);
    size_t seen = 0;
    for (gc_node_ref_t input : graph.withInputsOf(bodyValue)) {
        const GCNode *node = graph.node(input);
        if (node == nullptr || node->kind == GCNodeKind::Port || node->dataIndex == 0 ||
            !regions.within(regions.controlOf(input), bodyRegion)) {
            return false;
        }
        const size_t component = fillBody->slots()[seen];
        if (component >= loop.stateCount || stateSlots[component] != 0) {
            return false;
        }
        stateSlots[component] = node->dataIndex;
        ++seen;
    }
    if (seen != loop.stateCount) {
        return false;
    }
    for (gc_data_idx_t slot : stateSlots) {
        if (slot == 0) {
            return false;
        }
    }
    // Where the FILL is in the plan, so that a back end that walks the plan skips it.
    elidedTopo = plan.topo.size();
    for (size_t i = 0; i < plan.topo.size(); ++i) {
        if (plan.topo[i] == bodyValue) {
            elidedTopo = i;
            break;
        }
    }
    if (elidedTopo == plan.topo.size()) {
        return false;
    }
    loop.stateComponents  = true;
    loop.stateSlotsOffset = static_cast<uint32_t>(plan.loopSlots.size());
    for (gc_data_idx_t slot : stateSlots) {
        plan.loopSlots.push_back(slot);
    }
    return true;
}

/// The loops of `graph` that are regions of it: where their ranges are in the plan and which
/// slots a round reads and writes.
void buildLoopTables(const GCGraph &graph, ExecPlan &plan) {
    struct Event {
        uint32_t position;
        uint32_t loopEnd;
        uint32_t code;
    };
    std::vector<Event> events;
    std::optional<ControlRegions> regions;
    for (uint32_t li = 0; li < plan.topo.size(); ++li) {
        const gc_node_ref_t ref = plan.topo[li];
        const GCNode *node      = graph.node(ref);
        if (node == nullptr || node->kind != GCNodeKind::Loop) {
            continue;
        }
        const uint32_t condRegion = graph.loopRegionOf(ref, GCRegionKind::LoopCond);
        const uint32_t bodyRegion = graph.loopRegionOf(ref, GCRegionKind::LoopBody);
        if (bodyRegion == 0) {
            continue; // a loop of two graphs: the back end calls them
        }
        if (!regions) {
            regions.emplace(graph);
        }
        const camel::core::LoopKind kind = graph.loopKindOf(ref);
        ExecLoop loop;
        loop.position        = li;
        loop.iterates        = camel::core::loopHasIterable(kind);
        loop.hasCond         = condRegion != 0;
        loop.collectionCount = graph.loopCollectionCountOf(ref);
        loop.stateCount =
            static_cast<uint32_t>(graph.normInputsOf(ref).size() - (loop.iterates ? 1 : 0));
        loop.stateSlot = node->dataIndex;
        if (loop.stateCount > 1 || loop.collectionCount != 0) {
            loop.valueType = static_cast<const camel::core::type::TupleType *>(node->dataType());
        }
        loop.origin     = graph.nodeOrigin(ref);
        const auto norm = graph.normInputsOf(ref);
        if (loop.iterates) {
            loop.iterableSlot =
                dataIndexOf(graph, valueBehindGates(graph, norm.front()));
            if (graph.loopIterableFormOf(ref) == camel::core::LoopIterableForm::Interval) {
                // The sequence is the pair of the interval's two bounds: the loop computes the
                // element of a round from them and the round number, and no array of the interval
                // is ever built (`for i in a..b`, design principles 4.2).
                loop.intervalIterates = true;
                const auto bounds     = boundSlotsOf(graph, norm.front());
                ASSERT(
                    bounds.has_value(),
                    "An interval a loop iterates is the pair of its two bounds.");
                loop.intervalFromSlot = bounds->first;
                loop.intervalToSlot   = bounds->second;
            }
        }
        const auto results = graph.withInputsOf(ref);
        ASSERT(
            results.size() == (loop.hasCond ? 3u : 2u),
            "A loop of regions reads the results of its regions and its round.");
        loop.roundSlot      = dataIndexOf(graph, results.back());
        loop.bodyResultSlot = dataIndexOf(graph, valueBehindGates(graph, results[0]));
        if (loop.hasCond) {
            loop.condResultSlot = dataIndexOf(graph, valueBehindGates(graph, results[1]));
        }
        loop.slotsOffset = static_cast<uint32_t>(plan.loopSlots.size());
        for (size_t j = 0; j < loop.stateCount; ++j) {
            plan.loopSlots.push_back(dataIndexOf(graph, norm[j + (loop.iterates ? 1 : 0)]));
        }
        if (loop.hasCond) {
            for (gc_node_ref_t port : graph.regionPorts(condRegion)) {
                plan.loopSlots.push_back(dataIndexOf(graph, port));
            }
        }
        for (gc_node_ref_t port : graph.regionPorts(bodyRegion)) {
            plan.loopSlots.push_back(dataIndexOf(graph, port));
        }
        // The ranges: the nodes of the condition, then those of the body, then the LOOP node.
        uint32_t condFirst = li, condLast = li, bodyFirst = li, bodyLast = li;
        for (uint32_t at = 0; at < li; ++at) {
            const uint32_t control = regions->controlOf(plan.topo[at]);
            if (condRegion != 0 && regions->within(control, condRegion)) {
                condFirst = std::min(condFirst, at);
                condLast  = at + 1;
            } else if (regions->within(control, bodyRegion)) {
                bodyFirst = std::min(bodyFirst, at);
                bodyLast  = at + 1;
            }
        }
        const bool condEmpty = condLast == li && condFirst == li;
        const bool bodyEmpty = bodyLast == li && bodyFirst == li;
        ASSERT(
            (condEmpty || bodyEmpty || condLast == bodyFirst) && (bodyEmpty || bodyLast == li) &&
                (!bodyEmpty || condEmpty || condLast == li),
            "The nodes of a loop are not one range in the execution order.");
        // A loop whose body ends in a tuple nothing reads keeps the state as its components
        // instead of making the tuple again every round (ExecLoop::stateComponents).
        size_t elidedTopo = 0;
        if (planStateComponents(graph, *regions, plan, bodyRegion, ref, loop, elidedTopo)) {
            if (plan.elidedAt.empty()) {
                plan.elidedAt.assign(plan.topo.size(), 0);
            }
            plan.elidedAt[elidedTopo] = 1;
        }
        loop.begin   = !condEmpty ? condFirst : (!bodyEmpty ? bodyFirst : li);
        loop.condEnd = !bodyEmpty ? bodyFirst : li;
        const uint32_t index = static_cast<uint32_t>(plan.loops.size());
        events.push_back({loop.begin, li, index * 3 + static_cast<uint32_t>(ExecLoopEvent::Begin)});
        events.push_back(
            {loop.condEnd, li, index * 3 + static_cast<uint32_t>(ExecLoopEvent::Test)});
        events.push_back({li, li, index * 3 + static_cast<uint32_t>(ExecLoopEvent::End)});
        plan.loops.push_back(loop);
    }
    if (plan.loops.empty()) {
        return;
    }
    for (ExecLoop &loop : plan.loops) {
        loop.slots = plan.loopSlots.data() + loop.slotsOffset;
        if (loop.stateComponents) {
            loop.stateSlots = plan.loopSlots.data() + loop.stateSlotsOffset;
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
        if (a.position != b.position) {
            return a.position < b.position;
        }
        if (a.loopEnd != b.loopEnd) {
            return a.loopEnd > b.loopEnd; // the outer loop first
        }
        return a.code % 3 < b.code % 3;
    });
    plan.loopEventOffsets.assign(plan.topo.size() + 1, 0);
    for (const Event &event : events) {
        ++plan.loopEventOffsets[event.position + 1];
        plan.loopEvents.push_back(event.code);
    }
    for (size_t i = 1; i < plan.loopEventOffsets.size(); ++i) {
        plan.loopEventOffsets[i] += plan.loopEventOffsets[i - 1];
    }
}

} // namespace

std::unique_ptr<ExecPlan> buildExecPlan(const GCGraph &graph) {
    // The helpers of graph_runtime_support read the graph through its const interface only.
    auto *mutableGraph = const_cast<GCGraph *>(&graph);
    auto plan          = std::make_unique<ExecPlan>();
    plan->graph        = &graph;
    plan->topo         = camel::execute::computeExecutionOrder(&graph);

    const size_t count        = plan->topo.size();
    const bool hasValueReturn = graph.funcType() != nullptr && graph.funcType()->hasExitType() &&
                                graph.funcType()->exitType() != camel::core::type::Type::Void();

    plan->directCallTargetIds.resize(count, kInvalidGraphId);
    plan->directCallTailEligible.resize(count, 0);
    plan->directCallFeedsTailJoin.resize(count, 0);
    plan->directCallArgOffsets.resize(count + 1, 0);
    plan->branchArmEntryOffsets.resize(count + 1, 0);
    plan->dataIndexByRef.resize(graph.nodeCount(), 0);
    plan->resolvedOpers = std::make_unique<std::atomic<operator_t>[]>(graph.nodeCount());
    plan->topoIndexByRef.assign(graph.nodeCount(), ExecPlan::kNotInPlan);
    for (size_t idx = 0; idx < count; ++idx) {
        plan->topoIndexByRef[plan->topo[idx]] = static_cast<uint32_t>(idx);
    }
    plan->tailValueRef = hasValueReturn ? camel::execute::resolveRuntimeTailValueRef(mutableGraph)
                                        : kInvalidNodeRef;
    const GCNode *tailValueNode =
        plan->tailValueRef != kInvalidNodeRef ? graph.node(plan->tailValueRef) : nullptr;
    if (!tailValueNode || tailValueNode->dataIndex == 0) {
        plan->tailValueRef = kInvalidNodeRef;
        tailValueNode      = nullptr;
    }
    plan->tailValueIsJoin    = tailValueNode != nullptr && tailValueNode->kind == GCNodeKind::Join;
    plan->tailValueTopoIndex = count;
    for (size_t idx = 0; idx < count; ++idx) {
        if (plan->topo[idx] == plan->tailValueRef) {
            plan->tailValueTopoIndex = idx;
            break;
        }
    }
    const bool anchorOk = plan->tailValueTopoIndex < count &&
                          camel::execute::hasOnlyTrivialRuntimeTailSuffixAfter(
                              &graph,
                              plan->topo,
                              plan->tailValueTopoIndex);

    // A call whose result reaches the tail JOIN only through forwarding GATEs and nested JOINs
    // is still in tail position.
    const std::vector<gc_node_ref_t> tailJoinArmValues =
        anchorOk && plan->tailValueIsJoin
            ? camel::execute::collectRuntimeTailJoinArmValues(mutableGraph, plan->tailValueRef)
            : std::vector<gc_node_ref_t>{};

    const camel::execute::BranchArmEntries armEntries(graph, plan->topo);
    for (size_t idx = 0; idx < count; ++idx) {
        const gc_node_ref_t ref = plan->topo[idx];
        const GCNode *node      = graph.node(ref);
        if (node) {
            plan->dataIndexByRef[ref] = node->dataIndex;
        }
        plan->directCallArgOffsets[idx]    = static_cast<uint32_t>(plan->directCallArgSlots.size());
        plan->branchArmEntryOffsets[idx]   = static_cast<uint32_t>(plan->branchArmEntries.size());
        if (node && node->kind == GCNodeKind::Brch) {
            const size_t armCount = graph.armCountOf(ref);
            for (size_t arm = 0; arm < armCount; ++arm) {
                plan->branchArmEntries.push_back(armEntries.entry(ref, arm));
                plan->branchArmTails.push_back(armEntries.tail(ref, arm));
            }
        }
        if (!node || node->kind != GCNodeKind::Func) {
            continue;
        }
        plan->directCallTargetIds[idx] = graph.calleeIdOf(ref);
        ASSERT(
            plan->directCallTargetIds[idx] != kInvalidGraphId,
            "A direct FUNC target must be materialized into runtime metadata.");
        for (gc_node_ref_t argRef : graph.normInputsOf(ref)) {
            plan->directCallArgSlots.push_back(dataIndexOf(graph, argRef));
        }
        for (gc_node_ref_t argRef : graph.withInputsOf(ref)) {
            plan->directCallArgSlots.push_back(dataIndexOf(graph, argRef));
        }
        plan->directCallTailEligible[idx]  = anchorOk && (ref == plan->tailValueRef);
        plan->directCallFeedsTailJoin[idx] =
            std::find(tailJoinArmValues.begin(), tailJoinArmValues.end(), ref) !=
            tailJoinArmValues.end();
    }
    plan->tailAnchorOk = anchorOk;
    if (anchorOk) {
        if (plan->tailValueIsJoin) {
            plan->tailJoins.push_back(plan->tailValueRef);
        }
        for (const gc_node_ref_t value : tailJoinArmValues) {
            if (graph.node(value)->kind == GCNodeKind::Join) {
                plan->tailJoins.push_back(value);
            }
        }
        for (size_t idx = 0; idx < count; ++idx) {
            if (plan->directCallTailEligible[idx] || plan->directCallFeedsTailJoin[idx]) {
                plan->tailCalls.push_back(plan->topo[idx]);
            }
        }
        std::ranges::sort(plan->tailCalls);
    }
    plan->directCallArgOffsets[count]  = static_cast<uint32_t>(plan->directCallArgSlots.size());
    plan->branchArmEntryOffsets[count] = static_cast<uint32_t>(plan->branchArmEntries.size());
    buildLoopTables(graph, *plan);

    for (gc_node_ref_t portRef : graph.normPorts()) {
        plan->calleePortSlots.push_back(dataIndexOf(graph, portRef));
    }
    for (gc_node_ref_t portRef : graph.withPorts()) {
        plan->calleePortSlots.push_back(dataIndexOf(graph, portRef));
    }
    return plan;
}

} // namespace camel::runtime
