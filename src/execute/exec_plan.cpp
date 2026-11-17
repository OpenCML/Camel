// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Computation of the execution plan of a graph version (camel/runtime/exec_plan.h).
 */

#include "camel/runtime/exec_plan.h"

#include "camel/core/context/frame.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/ref.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/profile/profile.h"
#include "camel/runtime/control_regions.h"

#include <algorithm>
#include <format>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace camel::runtime {

namespace {

gc_data_idx_t dataIndexOf(const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    ASSERT(node != nullptr, "Execution plan node lookup resolved to null.");
    return node->dataIndex;
}

/// One bound of the pair of integers that is the sequence of a loop that iterates an interval
/// (`camel::core::LoopIterableForm::Interval`): the bound's integer, and where the plan reads it
/// (ExecLoop::IntervalBound).
using IntervalBound = ExecLoop::IntervalBound;

/// The bound a node of the pair stands for. A bound the graph computes is the slot the node
/// writes; one nothing computes -- the constant the front end wrote, or the constant a pass folded
/// a computation into -- is the value itself, which the plan reads without a frame slot.
IntervalBound boundOf(const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    const auto *type   = node == nullptr ? nullptr : node->dataType();
    if (node != nullptr && node->kind == GCNodeKind::Data && node->dataIndex < 0 &&
        type != nullptr && type->code() == camel::core::type::TypeCode::Int64) {
        const auto slots   = graph.staticSlots();
        const size_t index = static_cast<size_t>(-static_cast<int64_t>(node->dataIndex));
        if (index != 0 && index < slots.size() && slots[index] != NullSlot) {
            return IntervalBound{
                .slot       = 0,
                .value      = camel::core::rtdata::fromSlot<int64_t>(slots[index]),
                .isConstant = true};
        }
    }
    return IntervalBound{.slot = dataIndexOf(graph, ref), .value = 0, .isConstant = false};
}

/// The two bounds of the interval, in order: the pair is a value, so the FILL the front end builds
/// for `for i in a..b` and the constant holding the same two integers (a pass may fold the pair)
/// are read alike (GCGraph::intervalBoundsOf, intervalPairValuesOf). Nothing when `ref` is not
/// such a pair.
std::optional<std::pair<IntervalBound, IntervalBound>>
intervalBoundsOf(const GCGraph &graph, gc_node_ref_t ref) {
    if (const auto pair = graph.intervalBoundsOf(ref)) {
        return std::pair{boundOf(graph, pair->first), boundOf(graph, pair->second)};
    }
    if (const auto values = graph.intervalPairValuesOf(ref)) {
        return std::pair{
            IntervalBound{.slot = 0, .value = values->first, .isConstant = true},
            IntervalBound{.slot = 0, .value = values->second, .isConstant = true}};
    }
    return std::nullopt;
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
        // A loop is its regions (design principles 3.10): a LOOP node without a body region is not
        // a loop this plan can run, and there is no other form for it to be.
        const uint32_t condRegion = graph.loopRegionOf(ref, GCRegionKind::LoopCond);
        const uint32_t bodyRegion = graph.loopRegionOf(ref, GCRegionKind::LoopBody);
        if (bodyRegion == 0) {
            continue;
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
            loop.iterableSlot = dataIndexOf(graph, valueBehindGates(graph, norm.front()));
            if (graph.loopIterableFormOf(ref) == camel::core::LoopIterableForm::Interval) {
                // The sequence is the pair of the interval's two bounds: the loop computes the
                // element of a round from them and the round number, and no array of the interval
                // is ever built (`for i in a..b`, design principles 4.2).
                loop.intervalIterates = true;
                // The pair is read where the iterable is, behind the gates that order it after
                // statements (line above). A bound of it that is a constant is read as that
                // constant; one the graph computes is read from its slot.
                const auto bounds = intervalBoundsOf(graph, valueBehindGates(graph, norm.front()));
                // A loop that iterates an interval and has no pair of bounds behind it is a graph
                // the front end never builds and no pass may make: the two integers it would read
                // are not there, and reading them anyway takes the trip count of the loop from
                // whatever the frame happens to hold -- a wrong number of rounds, or a number so
                // large that the run never ends, on one machine and not on another. It is reported
                // instead (main.cpp prints an exception from here as the compiler's own defect),
                // because ASSERT is compiled out of a release build and the alternative is that
                // read.
                if (!bounds.has_value()) {
                    throw std::runtime_error(
                        std::format(
                            "Runtime graph '{}' iterates an interval with no pair of bounds behind "
                            "node {} ({}): the two bounds of `for i in a..b` are the pair the loop "
                            "node reads first -- a FILL or the constant holding the same two "
                            "integers -- and a pass removed it. This is a defect of the compiler, "
                            "not an error of the program.",
                            graph.name(),
                            ref,
                            graph.nodeOrigin(ref)));
                }
                loop.intervalFrom = bounds->first;
                loop.intervalTo   = bounds->second;
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
        // The nodes of the loop: the nodes of its condition and those of its body. They are one
        // piece of the execution order -- one unbroken range that ends at the LOOP node that ends a
        // round -- because a round begins at the first of them and the LOOP node ends it: a node
        // that is not one of them, standing inside that range, runs as part of every round.
        //
        // Where the condition stands *inside* that range is not this question. A graph may make the
        // condition out of what the body's nodes make (`[body ...] [cond ...] [body ...]`), and the
        // set of the loop's nodes is still one range; the plan runs it because the Test fires where
        // the condition's nodes have run (ExecLoop::condEnd). What must not happen is a node of
        // somebody else in between, and `intruder` below is the first such node.
        //
        // A release build compiles the assertion out, and the tables this function makes are what
        // every back end runs with. So the same question is asked twice: the assertion stops a
        // debug build where the plan is made, and the error value below is the answer a release
        // build has
        // -- the plan is refused, the pass that asked for it fails instead of reporting success,
        // and no interpreter walks a table that says a round starts somewhere its nodes do not.
        const bool condEmpty     = condLast == li && condFirst == li;
        const bool bodyEmpty     = bodyLast == li && bodyFirst == li;
        const uint32_t spanFirst = std::min(condFirst, bodyFirst);
        const uint32_t spanEnd   = std::max(condLast, bodyLast);
        uint32_t intruder        = li;
        for (uint32_t at = spanFirst; at < li && intruder == li; ++at) {
            const uint32_t control = regions->controlOf(plan.topo[at]);
            const bool member      = (condRegion != 0 && regions->within(control, condRegion)) ||
                                     regions->within(control, bodyRegion);
            if (!member) {
                intruder = at;
            }
        }
        const bool oneRange = intruder == li;
        ASSERT(oneRange, "The nodes of a loop are not one range in the execution order.");
        const uint32_t index = static_cast<uint32_t>(plan.loops.size());
        if (!oneRange) {
            const gc_node_ref_t intruderRef = plan.topo[intruder];
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has a loop whose nodes are not one contiguous range in the "
                    "execution order: loop #{} is the LOOP node {} ({}), at position {}, and its "
                    "nodes are [{}, {}) -- the node {} ({}) at position {} is not one of them, and "
                    "it stands between the loop's nodes and the LOOP node that ends a round. A "
                    "round "
                    "begins at the first of the loop's nodes and the LOOP node ends it, so a plan "
                    "this table describes would run that node as part of every round. This plan "
                    "cannot be run. This is a defect of the compiler, not an error of the program.",
                    graph.name(),
                    index,
                    ref,
                    graph.nodeOrigin(ref),
                    li,
                    spanFirst,
                    spanEnd,
                    intruderRef,
                    graph.nodeOrigin(intruderRef),
                    intruder));
        }
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

/// The operators that use a variable's cell without keeping the value in it: what a cell may be
/// used by and stay a variable (`:var/new`, `:var/get`, `:var/peek`, `:var/take`, the assignment
/// operators). Any other user -- a call handed the `var`, a capture port -- takes the cell out of
/// the graph where its value's observers can be seen.
bool isVariableOperator(std::string_view uri) {
    return uri.starts_with(":var/") || uri.starts_with(":op/assn");
}

/// How many nodes the escape walk visits before it gives up, and how many steps the order walk
/// takes before it does. Both limits bound compile time; neither is a licence to answer
/// "proved" -- a walk that stopped early has not shown anything, and the answer that is true of
/// every graph it did not finish is the conservative one.
constexpr size_t kEscapeWalkLimit = 256;
constexpr size_t kOrderWalkLimit  = 4096;

/// Whether the value in the cell `cell` may be seen by something the graph does not show: the cell
/// escapes when a user other than the variable's own operators touches it. GATEs are transparent
/// (they forward and order; they do not keep a value).
bool cellEscapes(
    const GCGraph &graph, const std::vector<std::vector<gc_node_ref_t>> &users, gc_node_ref_t cell,
    std::vector<uint8_t> &cache) {
    if (cell < cache.size() && cache[cell] != 0) {
        return cache[cell] == 2;
    }
    bool escapes = false;
    std::vector<gc_node_ref_t> pending{cell};
    std::unordered_set<gc_node_ref_t> seen;
    while (!pending.empty() && seen.size() < kEscapeWalkLimit) {
        const gc_node_ref_t at = pending.back();
        pending.pop_back();
        if (!seen.insert(at).second) {
            continue;
        }
        for (const gc_node_ref_t user : users[at]) {
            const GCNode *node = graph.node(user);
            if (node == nullptr) {
                continue;
            }
            if (node->kind == GCNodeKind::Gate) {
                pending.push_back(user);
                continue;
            }
            if (node->kind != GCNodeKind::Oper) {
                escapes = true;
                break;
            }
            const auto *body = graph.nodeBodyAs<GCOperBody>(user);
            if (body == nullptr || !isVariableOperator(body->uri())) {
                escapes = true;
                break;
            }
        }
        if (escapes) {
            break;
        }
    }
    if (!pending.empty()) {
        // The walk stopped at the limit rather than at the end of the users: whatever lies beyond
        // it may see the value, and one node is enough. A graph with more than the limit nodes
        // between a cell and what escapes it is not a graph this walk can settle, so it answers
        // what is true of all of them (design principles 10.5: uniqueness is proved, not assumed).
        escapes = true;
    }
    if (cell < cache.size()) {
        cache[cell] = escapes ? 2 : 1;
    }
    return escapes;
}

/// Whether the update at `update` reaches every later user of `cell` along the graph's own edges.
/// The proof above is a linear one: it reads `plan.topo` as the order the nodes run in. Only the
/// graph's edges order two of its nodes -- a back end may run anything else in any order, and
/// `std::prl` does -- so a user of the cell the walk placed after the update counts only when the
/// update reaches it. One it does not reach may run first, read the value the update is about to
/// write into and keep it, and then "reuses the input's storage" is a claim no one can make.
bool laterUsersAreOrderedAfter(
    const std::vector<std::vector<gc_node_ref_t>> &successors,
    const std::vector<std::vector<gc_node_ref_t>> &users, const std::vector<size_t> &position,
    gc_node_ref_t cell, gc_node_ref_t update) {
    if (cell >= users.size()) {
        return false;
    }
    bool anything = false;
    for (const gc_node_ref_t user : users[cell]) {
        // The update's own `:var/take` is an input of the update and is ordered by it, and a
        // `:var/new` or assignment the walk does not reach leaves the cell's own answer in
        // charge; only a user that runs later in the plan's linear order is at issue here.
        if (user < position.size() && position[user] > position[update]) {
            anything = true;
            break;
        }
    }
    if (!anything) {
        return true;
    }
    std::unordered_set<gc_node_ref_t> reached;
    std::vector<gc_node_ref_t> pending{update};
    size_t steps = 0;
    while (!pending.empty()) {
        if (++steps > kOrderWalkLimit) {
            return false; // did not finish: the order is not established, so it is not claimed
        }
        const gc_node_ref_t at = pending.back();
        pending.pop_back();
        if (!reached.insert(at).second || at >= successors.size()) {
            continue;
        }
        for (const gc_node_ref_t next : successors[at]) {
            pending.push_back(next);
        }
    }
    for (const gc_node_ref_t user : users[cell]) {
        if (user < position.size() && position[user] > position[update] &&
            !reached.contains(user)) {
            return false;
        }
    }
    return true;
}

/// The cell a node reads or writes, when it is one of the variable's own operators.
gc_node_ref_t cellOfNode(const GCGraph &graph, gc_node_ref_t ref) {
    const auto norm = graph.normInputsOf(ref);
    if (norm.empty()) {
        return kInvalidNodeRef;
    }
    return valueBehindGates(graph, norm.front());
}

/// Whether the cell is a variable this graph declares (`var x = e` is `:var/new`). A cell that
/// comes from elsewhere -- a `var` parameter, a captured cell -- is a port of this graph: its
/// value is held outside it, so nothing in the graph can prove the old value has no observer.
bool cellDeclaredHere(const GCGraph &graph, gc_node_ref_t cell) {
    const GCNode *node = cell == kInvalidNodeRef ? nullptr : graph.node(cell);
    if (node == nullptr || node->kind != GCNodeKind::Oper) {
        return false;
    }
    const auto *body = graph.nodeBodyAs<GCOperBody>(cell);
    return body != nullptr && body->oper == camel::core::oper::VarNew;
}

/// Whether the value a variable starts with is one this graph makes: a `Copy` node (a clone of a
/// literal, made when the graph runs), a `Fill` node (a value built here), or a value slot of the
/// graph. A static value (a `Data` node that indexes the static area) is the program's constant:
/// it stays as it is and its variable's update copies. A value from elsewhere -- a call's result,
/// a read of another variable, a parameter -- has an owner the plan cannot see, so the variable's
/// first update copies as well.
bool initialValueMadeHere(const GCGraph &graph, gc_node_ref_t init) {
    const GCNode *node = init == kInvalidNodeRef ? nullptr : graph.node(init);
    if (node == nullptr) {
        return false;
    }
    switch (node->kind) {
    case GCNodeKind::Copy:
    case GCNodeKind::Fill:
        return true;
    case GCNodeKind::Data:
        return node->dataIndex >= 0;
    default:
        return false;
    }
}

/// Whether anything in the graph may hold the cell's value after it runs: a read whose value can
/// be kept (`:var/get`), or a use that is not the variable's own operator (a call, a store, a
/// capture). A read that cannot keep the value (`:var/peek`) and the update's own take and
/// assignment do not. A cell of this graph is judged in order instead (see planStorageReuse); a
/// cell from elsewhere is judged with this, because the graph may run again.
bool cellValueMayBeKept(
    const GCGraph &graph, const std::vector<std::vector<gc_node_ref_t>> &users,
    gc_node_ref_t cell) {
    if (cell >= users.size()) {
        return true;
    }
    for (const gc_node_ref_t user : users[cell]) {
        const GCNode *node = graph.node(user);
        if (node == nullptr || node->kind != GCNodeKind::Oper) {
            return true;
        }
        const auto *body = graph.nodeBodyAs<GCOperBody>(user);
        if (body != nullptr && body->uri() == ":var/get") {
            return true;
        }
    }
    return false;
}

/// What the uses walk hands to both tables: which value each node carries, who reads a value and
/// where, which nodes pack a value into a value of their own, and which values the program keeps.
/// Declared here because planStorageReuse runs the walk and both of its tables are filled from it.
struct ValueUses {
    std::vector<std::vector<gc_node_ref_t>> carried;   // by node ref
    std::vector<std::vector<gc_node_ref_t>> packersOf; // by node ref
    std::vector<std::vector<uint32_t>> usesAt;         // by node ref, topo positions
    std::vector<uint8_t> keptByProgram;                // by node ref
    std::vector<uint32_t> endOf; // by node ref: where the value stops mattering
};
void planValueUses(const GCGraph &graph, ExecPlan &plan, ValueUses &uses);
void planRefRows(const GCGraph &graph, ExecPlan &plan, const ValueUses &uses);

/// Where each in-place update runs and whether it reuses the storage of the value it is given
/// (docs/design/arrays-memory/README.cn.md 4.3.1). The update node takes its array from
/// `:var/take` of a cell; the plan walks the graph in execution order carrying, per cell, whether
/// the value the cell holds is one this variable's own update made and nothing has read since
/// (the runtime fact the cell used to carry as a mark, now decided once, here):
///
///   * a cell that escapes, or that holds a value of unknown origin, proves nothing: the update
///     asks the array itself at run time (StorageReuse::ByArrayCount);
///   * a read that keeps the value (`:var/get`), or a plain store (`:op/assn`), ends the proof:
///     the update copies (StorageReuse::Copy) and runs the plain form of the operator;
///   * otherwise the update reuses the input's storage (StorageReuse::Input) -- but only where
///     the graph's own edges order every later user of the cell after the update, because the
///     walk itself is linear and the back ends are not (laterUsersAreOrderedAfter).
///
/// A loop's state comes round to the next round (the LOOP node takes the value of the body), so
/// the walk runs twice: the second time, each loop's body starts from the state its end reached.
void planStorageReuse(const GCGraph &graph, ExecPlan &plan) {
    const size_t count = plan.topo.size();
    plan.storageReuseAt.assign(count, StorageReuse::None);
    // The uses of every value first: this walk and the references that come back read one answer
    // (the author's ruling of 2026-11-15 -- the last use is computed here, not written twice).
    ValueUses valueUses;
    planValueUses(graph, plan, valueUses);
    const auto &table      = camel::core::OperatorTable::instance();
    const size_t nodeCount = graph.nodeCount();
    std::vector<std::vector<gc_node_ref_t>> users(nodeCount);
    // The graph's own edges, the way a back end follows them: a node runs after its data, `with`
    // and control inputs, after the statement a sync edge writes before it, and after what an
    // order edge orders before it. Anything else the graph leaves unordered, and `std::prl` runs
    // it in any order (see laterUsersAreOrderedAfter). What the edges are is the one shared
    // answer (camel/execute/graph_runtime_support.h: executionPredecessorsOf); this walk only
    // turns it round, into who runs after whom.
    std::vector<std::vector<gc_node_ref_t>> successors(nodeCount);
    const auto noteSuccessor = [&](gc_node_ref_t from, gc_node_ref_t to) {
        if (from < successors.size()) {
            successors[from].push_back(to);
        }
    };
    std::vector<size_t> position(nodeCount, count);
    for (size_t idx = 0; idx < count; ++idx) {
        const gc_node_ref_t at = plan.topo[idx];
        if (at < position.size()) {
            position[at] = idx;
        }
        for (const camel::execute::ExecutionPredecessor &predecessor :
             camel::execute::executionPredecessorsOf(graph, at)) {
            // The two data roles (`normal` and `with`) are the same edge kind here (edges.cn.md
            // 1.5): both are a value the node reads, and both make the writer run first.
            if (predecessor.kind == camel::execute::ExecutionPredecessor::Kind::Data &&
                predecessor.node < users.size()) {
                users[predecessor.node].push_back(at);
            }
            noteSuccessor(predecessor.node, at);
        }
    }
    // A sync or order edge whose user the plan does not hold points at a node that does not run,
    // and it is in the index all the same: the walk below follows `successors` from the update it
    // starts at, and a node the plan does not hold can be the step between two that it does.
    for (const camel::runtime::GCGraphSyncEdge &edge : graph.syncEdges()) {
        if (edge.user >= position.size() || position[edge.user] == count) {
            noteSuccessor(edge.pred, edge.user);
        }
    }
    for (const camel::runtime::GCGraphOrderEdge &edge : graph.orderEdges()) {
        if (edge.user >= position.size() || position[edge.user] == count) {
            noteSuccessor(edge.pred, edge.user);
        }
    }
    std::vector<uint8_t> escapeCache(nodeCount, 0);
    std::vector<uint8_t> owned(nodeCount, 0);
    std::vector<std::vector<uint8_t>> loopEntry(plan.loops.size());
    for (int pass = 0; pass < 2; ++pass) {
        const bool final = pass == 1;
        std::fill(owned.begin(), owned.end(), 0);
        for (size_t idx = 0; idx < count; ++idx) {
            for (size_t li = 0; li < plan.loops.size(); ++li) {
                // A round starts from the state the round before left: the body's own update is
                // the writing the next round's update reads.
                if (plan.loops[li].begin == idx && !loopEntry[li].empty()) {
                    for (size_t c = 0; c < owned.size(); ++c) {
                        owned[c] = static_cast<uint8_t>(owned[c] | loopEntry[li][c]);
                    }
                }
            }
            const gc_node_ref_t at = plan.topo[idx];
            const GCNode *node     = graph.node(at);
            if (node == nullptr) {
                continue;
            }
            if (node->kind == GCNodeKind::Loop) {
                for (size_t li = 0; li < plan.loops.size(); ++li) {
                    if (plan.loops[li].position == idx) {
                        loopEntry[li] = owned;
                    }
                }
                continue;
            }
            if (node->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto *body = graph.nodeBodyAs<GCOperBody>(at);
            if (body == nullptr) {
                continue;
            }
            const std::string_view uri = body->uri();
            if (table.plainForm(uri).has_value()) {
                // An in-place form: an update node when its array comes from the cell's own
                // `:var/take`.
                const auto norm = graph.normInputsOf(at);
                const gc_node_ref_t taken =
                    norm.empty() ? kInvalidNodeRef : valueBehindGates(graph, norm.front());
                const GCNode *takenNode = taken == kInvalidNodeRef ? nullptr : graph.node(taken);
                if (takenNode != nullptr && takenNode->kind == GCNodeKind::Oper) {
                    const auto *takeBody = graph.nodeBodyAs<GCOperBody>(taken);
                    if (takeBody != nullptr && takeBody->uri() == ":var/take") {
                        const gc_node_ref_t cell = cellOfNode(graph, taken);
                        StorageReuse decision    = StorageReuse::Copy;
                        if (cell != kInvalidNodeRef) {
                            const bool escapes = cellEscapes(graph, users, cell, escapeCache);
                            if (!cellDeclaredHere(graph, cell)) {
                                // A cell this graph does not declare (a `var` parameter, a
                                // captured cell): the plan has no proof of who holds the old
                                // value, so the array's own count answers -- unless the graph
                                // reads the cell and may keep what it read, which the count
                                // cannot see (two names for one array are one array's count).
                                decision = escapes || cellValueMayBeKept(graph, users, cell)
                                               ? StorageReuse::Copy
                                               : StorageReuse::ByArrayCount;
                            } else if (escapes) {
                                decision = StorageReuse::Copy;
                            } else if (cell < owned.size() && owned[cell] != 0) {
                                // The proof holds in the plan's linear order; it holds under the
                                // partial order a parallel back end runs in only when the graph's
                                // own edges order every later user of the cell after this update.
                                // Where they do not, the array's count answers instead.
                                decision =
                                    laterUsersAreOrderedAfter(successors, users, position, cell, at)
                                        ? StorageReuse::Input
                                        : StorageReuse::ByArrayCount;
                            }
                        }
                        if (final) {
                            plan.storageReuseAt[idx] = decision;
                        }
                    }
                }
                continue;
            }
            if (uri == ":var/get") {
                const gc_node_ref_t cell = cellOfNode(graph, at);
                if (cell != kInvalidNodeRef && cell < owned.size()) {
                    owned[cell] = 0;
                }
            } else if (uri == ":var/new") {
                // `var v = e`: the node is the cell. When `e` is a value this graph makes, the
                // variable is its only holder from here on and its first update may reuse the
                // storage; when `e` comes from elsewhere, that first update copies.
                const auto norm = graph.normInputsOf(at);
                const gc_node_ref_t init =
                    norm.empty() ? kInvalidNodeRef : valueBehindGates(graph, norm.front());
                if (at < owned.size()) {
                    owned[at] = initialValueMadeHere(graph, init) ? 1 : 0;
                }
            } else if (uri.starts_with(":op/assn")) {
                const gc_node_ref_t cell = cellOfNode(graph, at);
                if (cell != kInvalidNodeRef && cell < owned.size()) {
                    owned[cell] = uri == ":op/assn_owned" ? 1 : 0;
                }
            }
        }
    }
    // What the plan decided, for the profile report (`std::profile`): the update nodes that reuse
    // the storage of the value they are given and the ones left to the array's own count.
    camel::profile::planUpdateMarks(
        plan.storageReuseCount(StorageReuse::Input),
        plan.storageReuseCount(StorageReuse::ByArrayCount),
        plan.storageReuseCount(StorageReuse::Copy));
    // The other table of the same walk: the references that come back, and the holds that balance
    // them. Same walk, same order -- `planRefRows` reads the uses above and the last-use column.
    planRefRows(graph, plan, valueUses);
}

/// A node that only carries a value on: a JOIN hands on the value of the arm that ran, a GATE
/// passes its input through, a port hands on what it was given (an exit port, its input's value),
/// and an operator whose result is a value an operand already holds -- `:enum/field` hands on the
/// field it read out of the variant -- hands on that operand's value. It produces nothing of its
/// own, so a reference to what it "holds" is a reference to the value behind it.
///
/// The three graph kinds are the fact themselves (a program cannot name a JOIN, a GATE or a port),
/// and the operator is asked for its declaration (`OperatorTable::handsOnOperandValue`):
/// there used to be a list of operator names here beside a table of the same names in the operator
/// table, and the two could say different things (author review 2026-11-17, section 2.8).
///
/// `:enum/field` declares it because of what the operator does with the field: `__enum_field__`
/// returns the slot the variant holds (src/builtin/operators/enum.cpp) and takes no reference for
/// it. Read as a producer, its result looked like a value born with a reference of its own, so the
/// plan released the value where it was read and again where the enum died -- one release too many
/// for a handle read out of a variant and used afterwards (a `Ref` a defunctionalized closure
/// carried, read after the call through the value).
///
/// `:op/idx_arr`'s element read declares it because what it answers is the slot the array holds
/// (`Array::getSlot`, src/builtin/operators/struct.cpp#__index_arr__) and not a value of its own: the
/// *uses* of what it hands on are the array's, so an element read is not where the box's reference
/// from the array comes back. That is all this predicate says. Read as a producer, `xs[0]` looked
/// like a value born with a reference of its own, and the plan gave that reference back at the
/// element's last use: the box was released while the array still held it (and while the box was
/// still read later), so `state::set(xs[0], 5)` wrote through a released handle and `state::get` on
/// either name answered "the value is not a reference".
///
/// The other half is not this predicate's: the read hands on a counted value, so the value it hands
/// on is a further holder of the box and the plan takes a reference for it at the read
/// (`takesTheReferenceForItsValue`, and the author's ruling of 2026-11-18 in the header, "the
/// references that come back"). Carrying and holding are two answers: which value a use belongs to,
/// and how many references the box has.
///
/// A row of a higher-rank array is NOT one of these, and its declaration says so
/// (`handsOnOnlyOutsideArrays`): `xs[i]` on a value with more than one dimension makes a value of
/// its own (a view that shares the block, or a copy for an array of references), so the plan counts
/// it as a producer's, as it counts any operator's output.
bool forwardsItsValue(const GCGraph &graph, gc_node_ref_t at) {
    const GCNode *node = graph.node(at);
    if (node == nullptr) {
        return false;
    }
    // The three carriers a graph is built with, not operators: a JOIN hands on the arm's value, a
    // GATE passes its input through and a port hands on what it was given. Their node kind is the
    // fact -- there is no operator record for one, because a program cannot name one.
    if (node->kind == GCNodeKind::Join || node->kind == GCNodeKind::Gate ||
        node->kind == GCNodeKind::Port) {
        return true;
    }
    if (node->kind != GCNodeKind::Oper) {
        return false;
    }
    const auto *body = graph.nodeBodyAs<GCOperBody>(at);
    if (body == nullptr) {
        return false;
    }
    // The same fact for an operator: it is the operator's own declaration, not a list of names
    // here, so the two cannot say different things (OperatorTable::handsOnOperandValue).
    const camel::core::OperatorTable &table = camel::core::OperatorTable::instance();
    if (!table.handsOnOperandValue(body->uri())) {
        return false;
    }
    if (!table.handsOnOnlyOutsideArrays(body->uri())) {
        return true;
    }
    // A row of a higher-rank value is a value of its own (a view that shares the block, or a copy
    // for an array of references), and the declaration says so: the hand-on is of an element.
    const camel::core::type::Type *type = node->dataType();
    return type != nullptr && type->code() != camel::core::type::TypeCode::Array;
}

/// A node that packs the values it is given into the value it makes: a FILL builds a closure tuple
/// (GCFillKind::FunctionClosure) or an aggregate (Tuple, Array, Struct) out of its inputs, and
/// `:enum/make` builds the variant it is given the tag and the fields of. The values they are given
/// do not stop being held when the node has run -- the value it made holds them -- so their
/// reference comes back where that value dies, not here.
///
/// `:enum/make` belongs on this list for the same reason a FILL does, and it was left off it: the
/// operator stores the fields it is given in the variant (`__enum_make__`,
/// src/builtin/operators/enum.cpp), so a counted handle among them is held by the enum. Read as an
/// ordinary use, its reference came back at the node that made the enum, and a handle no other node
/// read was released while the enum still held it -- the next operator to reach it through the
/// variant read "the value is not a reference" (a `Ref` captured by a function the defunctionalizer
/// lowered, read after the closure was made).
///
/// A call that answers a stream or a closure belongs here for the reason the FILL does: the callee
/// has not finished with the values it was handed. A generator's body runs on the pull, and the
/// stream the call answered is the closure over the state the body writes, so the `box` a generator
/// was given outlives the call that gave it -- `reading(box: state::Ref<int>)` answers `int..`, and
/// the second pull's `state::set` on it read "the value is not a reference" while the first pull had
/// worked. Read as an ordinary use, the one use of `state::ref(0)` in `main` was that call, so the
/// box's only reference came back at it (`stream.generator`: `std::nvm`, `std::fvm` and `std::prl`
/// failed; `std::jit` applied no row at that position and left the box alive, which is why the four
/// disagreed). A `Func` node is a direct call and its value is the callee's return (`GCFuncBody`),
/// which is what the test asks.
///
/// A call that answers an ordinary value does not belong here: the callee has finished, so what it
/// made holds nothing it was handed, and the reference of that use comes back at the call. That is
/// the reading `concurrent_readers_of_a_call_box` pins -- the box `make_box(41)` answers is handed
/// to two readers and each reader hands its own reference back where it read.
bool packsItsInputs(const GCGraph &graph, gc_node_ref_t at) {
    const GCNode *node = graph.node(at);
    if (node == nullptr) {
        return false;
    }
    if (node->kind == GCNodeKind::Fill) {
        return true;
    }
    if (node->kind == GCNodeKind::Func) {
        const camel::core::type::Type *answered =
            node->dataType(); // the callee's return type, which is the value the node makes
        if (answered == nullptr) {
            return false;
        }
        const camel::core::type::TypeCode code = answered->code();
        return code == camel::core::type::TypeCode::Stream ||
               code == camel::core::type::TypeCode::Function;
    }
    if (node->kind != GCNodeKind::Oper) {
        return false;
    }
    const auto *body = graph.nodeBodyAs<GCOperBody>(at);
    return body != nullptr && body->uri() == ":enum/make";
}

/// Whether the node reads `value` and passes it on instead of ending it, so that what happens to
/// the reference next is not this plan's to say: a direct call hands every value it reads to the
/// callee's frame, a call through a function value hands every value it reads except the one it
/// runs, and a field access (`Accs`, `.0` on a tuple, `s.key` on a struct) writes the field into
/// its own slot, which is a value the reader was packed into.
///
/// It matters for a *packing* value and for the references the packing value holds, which is why it
/// is asked where the packing value's end is decided rather than at the use. A callee that captures
/// what it was given holds it after the call has returned -- `unfold` is the shape: it answers a
/// closure that captures the `step` it was handed -- and a field access reads the tuple one of its
/// fields at a time, so the value the reference was packed into is still the holder after the
/// access. Read either way, this plan called the read the packing value's death: a box a closure
/// held came back at the second `unfold` call of a `tee` (and, with `std::opt`, at the field access
/// that unpacked the closure tuple), and the first `state::get` through the closure answered "the
/// value is not a reference".
///
/// The one value a call *does* end is the callee it runs: `bump()` on a closure that captured the
/// box reads the closure and nothing keeps it past that call, so the capture's reference still
/// comes back at the closure's last use (`ref_one_cell_two_holders`, feat/tools
/// `plan_refs.closure_holder` pins exactly that). The callee is a `with` input of the call, which
/// is why the answer is read through the values each input carries: the call ends the value its
/// callee input carries, and passes on the rest.
///
/// A value read by a call and *not* packed into anything keeps the plan's ordinary answer (the
/// reference of that use comes back at the call): each use holds a reference of its own while the
/// call runs, and a callee that keeps an argument is the callee's to hold, not this graph's to
/// give.
bool passesWhatItReadsOn(
    const GCGraph &graph, gc_node_ref_t at, gc_node_ref_t value,
    const std::vector<std::vector<gc_node_ref_t>> &carried) {
    const GCNode *node = graph.node(at);
    if (node == nullptr) {
        return false;
    }
    switch (node->kind) {
    case GCNodeKind::Accs:
        return true;
    case GCNodeKind::Func:
        return true;
    case GCNodeKind::Call: {
        for (const gc_node_ref_t callee : graph.withInputsOf(at)) {
            if (callee < carried.size() &&
                std::find(carried[callee].begin(), carried[callee].end(), value) !=
                    carried[callee].end()) {
                return false; // the value the call runs: this call is where it ends
            }
        }
        return true;
    }
    default:
        return false;
    }
}

/// The data predecessors of `at` whose values it reads **as values**: all of them, except for a
/// region's exit `GATE`, whose first normal input is the value it forwards and whose normal inputs
/// after it are the reads it takes of the region's writes (`Builder::layRegionWriteEdges`, design
/// principles 3.1, the author's ruling of 2026-11-17: "第一个输入是区域交出的值，其余输入是区域里按
/// 效应推断会写外部资源或句柄的节点"). The gate *waits* on those writes -- that wait is the whole of
/// what makes them live, and it is what `liveNodesOf` reads -- and hands nothing of theirs on, so it
/// neither carries their value (`carried`) nor holds a reference to it (`usesAt`).
///
/// Read as uses, they held a reference the gate never takes, and the plan's answer disagreed with
/// the run: `ref.reduce_takes_its_initial` laid the accumulator's give-back at the exit gate as well
/// as at the read and took the second hold that pays for it, and std::fvm and std::jit -- which
/// apply no rows at a gate's position, because a gate compiles to nothing -- never gave it back
/// (`released=0 live=1 disposed=1`). The same misreading turned the operator's own `void` value in
/// `ref.foreach_gives_back` from "nothing reads it" into a use at the exit, which moved its
/// give-back off the operator's position. A gate's trailing input is a wait, not a use.
std::vector<gc_node_ref_t> dataPredecessorsReadAsValues(const GCGraph &graph, gc_node_ref_t at) {
    // The reads a gate takes and hands nothing of on: its normal inputs after the first. The first
    // is the value, so it stays; and the list is deduplicated where it is laid, so one node cannot
    // be both.
    std::vector<gc_node_ref_t> waits;
    if (const GCNode *node = graph.node(at); node != nullptr && node->kind == GCNodeKind::Gate) {
        const std::span<const gc_node_ref_t> norms = graph.normInputsOf(at);
        for (size_t i = 1; i < norms.size(); ++i) {
            waits.push_back(norms[i]);
        }
    }
    std::vector<gc_node_ref_t> read;
    for (const camel::execute::ExecutionPredecessor &predecessor :
         camel::execute::executionPredecessorsOf(graph, at)) {
        if (predecessor.kind != camel::execute::ExecutionPredecessor::Kind::Data ||
            predecessor.node >= graph.nodeCount()) {
            continue;
        }
        if (std::find(waits.begin(), waits.end(), predecessor.node) != waits.end()) {
            continue;
        }
        read.push_back(predecessor.node);
    }
    return read;
}

/// The uses of every value, and route 4's "position of the last use" column.
///
/// The rule is written out in the header; what this function adds is the one thing the edges have
/// to be walked for: a JOIN's uses belong to the arms' values, because the JOIN hands on one of
/// them, and only that arm's value is the one whose reference comes back.
///
/// It is called from planStorageReuse, as the author's ruling of 2026-11-15 requires ("the last use
/// is computed by it, not written a second time"): the two walks read the same edges
/// (executionPredecessorsOf, the one shared answer to "whose value runs first"), so they are one
/// walk with two tables rather than two walks that must agree.
void planValueUses(const GCGraph &graph, ExecPlan &plan, ValueUses &uses) {
    const size_t count     = plan.topo.size();
    const size_t nodeCount = graph.nodeCount();
    auto &carried          = uses.carried;
    auto &packersOf        = uses.packersOf;
    auto &usesAt           = uses.usesAt;
    auto &keptByProgram    = uses.keptByProgram;
    auto &endOf            = uses.endOf;
    carried.assign(nodeCount, {});
    packersOf.assign(nodeCount, {});
    usesAt.assign(nodeCount, {});
    keptByProgram.assign(nodeCount, 0);
    endOf.assign(nodeCount, 0);
    plan.lastUseAt.assign(count, ExecPlan::kNoLastUse);

    // The values each node carries: `{its own}` for a producer, the union of its inputs for a node
    // that only carries a value on (a JOIN, a GATE, a port). A constant carries nothing: the
    // program holds it, and the graph neither takes nor gives it. A region's exit GATE carries only
    // what its **first** input carries: the normal inputs after it are the writes it waits on, and
    // `dataPredecessorsReadAsValues` says why that is not a use.
    for (size_t idx = 0; idx < count; ++idx) {
        const gc_node_ref_t at = plan.topo[idx];
        if (at >= nodeCount) {
            continue;
        }
        const GCNode *node = graph.node(at);
        if (node == nullptr || node->kind == GCNodeKind::Data) {
            continue; // a constant is the program's: the graph neither takes nor gives it
        }
        if (!forwardsItsValue(graph, at)) {
            carried[at].push_back(at);
            continue;
        }
        for (const gc_node_ref_t predecessor : dataPredecessorsReadAsValues(graph, at)) {
            for (const gc_node_ref_t value : carried[predecessor]) {
                if (std::find(carried[at].begin(), carried[at].end(), value) == carried[at].end()) {
                    carried[at].push_back(value);
                }
            }
        }
    }

    // Who keeps what out of the graph: the program, when the value is the graph's result. The plan
    // resolves the result through the forwarding nodes (tailValueRef), so the value named there is
    // the one whose reference the caller takes over.
    if (plan.tailValueRef != kInvalidNodeRef && plan.tailValueRef < nodeCount) {
        for (const gc_node_ref_t value : carried[plan.tailValueRef]) {
            keptByProgram[value] = 1;
        }
    }
    for (const gc_node_ref_t tailCall : plan.tailCalls) {
        if (tailCall < nodeCount) {
            keptByProgram[tailCall] = 1;
        }
    }

    // A value's uses, by position: every node that reads it (through forwarding) is one use. A use
    // by a node that packs its inputs (`packsItsInputs`) is also recorded against that node,
    // because the reference is then held by the value the packing node made and comes back when
    // *that* dies. A read a gate takes to order a write is not one of these (`dataPredecessorsReadAsValues`).
    for (size_t idx = 0; idx < count; ++idx) {
        const gc_node_ref_t at = plan.topo[idx];
        if (at >= nodeCount) {
            continue;
        }
        const bool packs = packsItsInputs(graph, at);
        for (const gc_node_ref_t predecessor : dataPredecessorsReadAsValues(graph, at)) {
            for (const gc_node_ref_t value : carried[predecessor]) {
                usesAt[value].push_back(static_cast<uint32_t>(idx));
                if (packs && std::find(packersOf[value].begin(), packersOf[value].end(), at) ==
                                 packersOf[value].end()) {
                    packersOf[value].push_back(at);
                }
            }
        }
    }

    // A value packed into a value the program keeps is kept too. The packing value carries the
    // reference out of the graph -- a closure the program holds, the aggregate the result is
    // returned in -- so it does not come back here, and a give-back placed at the packing value's
    // in-graph end would release a reference that value still holds. What that costs a reader: a
    // box packed into an array, and the array packed into a closure the graph returns, was given
    // back at the array's last read in the graph (a `state::set` on an element), so the box was
    // released while the closure still held it and the call's `state::get` answered "the value is
    // not a reference". Read in reverse topo order, because a packing node comes later in the order
    // than the values it packs, so a chain of packings settles in one pass.
    for (size_t idx = count; idx-- > 0;) {
        const gc_node_ref_t value = plan.topo[idx];
        if (value >= nodeCount || keptByProgram[value] != 0) {
            continue;
        }
        for (const gc_node_ref_t packer : packersOf[value]) {
            if (packer >= nodeCount) {
                continue;
            }
            if (keptByProgram[packer] != 0) {
                keptByProgram[value] = 1;
                break;
            }
        }
    }

    // When each value dies, which is where the references it packed into another value come back:
    // after its last use, and -- when it was itself packed into a value -- after that value dies.
    // Read in reverse topo order, so a packing node (later in the order than the values it packs)
    // has its own end computed first.
    for (size_t idx = count; idx-- > 0;) {
        const gc_node_ref_t at = plan.topo[idx];
        if (at >= nodeCount) {
            continue;
        }
        uint32_t end = static_cast<uint32_t>(idx);
        for (const uint32_t usePosition : usesAt[at]) {
            end = std::max(end, usePosition);
        }
        for (const gc_node_ref_t packer : packersOf[at]) {
            end = std::max(end, endOf[packer]);
        }
        endOf[at] = end;
    }

    // Route 4's column, filled from the same walk: for every value this graph produces, the
    // position after which it is never read again. A packing value's end is the end of what it
    // packed, which is why `endOf` is read in reverse above; a value nothing reads has no such
    // position and says so instead of naming its own.
    for (size_t idx = 0; idx < count; ++idx) {
        const gc_node_ref_t value = plan.topo[idx];
        if (value >= nodeCount || carried[value].empty() || carried[value].front() != value) {
            continue;
        }
        if (!usesAt[value].empty()) {
            plan.lastUseAt[idx] = endOf[value];
        }
    }
}

/// Whether the plan runs the position `at` more than once while the value made at `produced` is
/// held from outside that repetition: a loop's round runs its range again, and this value's
/// production is not in the range that repeats here.
///
/// The count is one last use on the execution order (design principles 10.5; the author's ruling of
/// 2026-11-15: the analysis is the plan's, once, and every back end follows it), and the execution
/// order of a loop is not linear -- its round's range [begin, position] is run again. That changes
/// nothing for a value the round itself makes: its holds and its give-backs are both inside the
/// range, so they balance per round. It changes everything for a value the range reads from
/// outside: the plan takes its holds once, before the loop, while the use recurs every round, so a
/// give-back placed at that use releases the value at the end of the first round and the second
/// round reads a value the count already said was dead. With a `Ref` handle that is
/// `state::get` on a disposed box, "the value is not a reference", on the second round of a loop
/// that read it correctly on the first.
///
/// The answer for such a use is not another position inside the round: the loop's frame is one of
/// the value's holders until the loop ends, and the one position of the plan that runs once at that
/// moment is the LOOP node's own position -- every other position the round runs is run again, and
/// the LOOP node is not an operator, so the count is not applied there by a back end. It is applied
/// there by the loop itself: `loopEnd` (camel/execute/loop_frame.h) hands the rows laid at the LOOP
/// node's position to `applyRefEntriesForLoopExit` when the loop leaves (author review 2026-11-16,
/// item 6, the give-back at the loop's exit). This function names that loop -- the outermost one
/// whose round runs the use and does not make the value, because that is the last round the value
/// must live through -- and `nullopt` when the use is not inside such a loop.
std::optional<uint32_t> loopRepeatingOver(const ExecPlan &plan, size_t produced, size_t at) {
    std::optional<uint32_t> exit;
    for (const ExecLoop &loop : plan.loops) {
        if (at < loop.begin || at > loop.position) {
            continue;
        }
        if (produced >= loop.begin && produced <= loop.position) {
            continue; // the value is one of the round's own: hold and give-back are per round
        }
        if (!exit || loop.position > *exit) {
            exit = loop.position;
        }
    }
    return exit;
}

/// The type code of the reference handle the runtime counts (`state::Ref`); defined below, beside
/// the filter that reads it at run time, so that the plan and the count ask one question.
camel::core::type::TypeCode countedHandleCode();

/// Whether a value of this type code is one the count owns (`state::Ref`). Defined once, beside
/// `countedHandleCode`, and asked by the plan below and by the run at the rows: it was written at
/// each of those three sites and one of the three would have changed first (author review
/// 2026-11-17, item 9).
bool isCountedHandle(camel::core::type::TypeCode code) noexcept;

/// Whether the value the node makes is a *further holder* of an object an operand already holds: the
/// node answers a value it was given, and the operator takes no reference for it. The count is the
/// number of holders (design principles 10.5), so such a value has a reference of its own, and the
/// plan is where it is taken -- the operator does not take it.
///
/// The author's ruling of 2026-11-18 (`812afad5c`), answering the question this lane's predecessor
/// raised in `docs/handover/reviews/questions-2026-11-17-two-rulings.cn.md`: "an element read of a
/// counted value is a further holder, so the plan retains there and the run-time skip goes". Read
/// out of an array, a counted element is one holder (the array holds it) plus one (the value the read
/// hands on), so the plan takes a hold at the read and gives it back where that value is last read.
///
/// Two facts answer it, and they are declarations the tree already has:
///
///   * what the operator says it hands on (`OperatorTable::handsOnOperandValue`): `:op/idx_arr`
///     answers the slot the array holds and `:enum/field` the slot the variant holds, and neither
///     takes a reference for it. `carried` still names what the *uses* belong to -- the array, or the
///     variant -- so what this adds is the holder, not a second reading of who carries what.
///   * a counted result with a counted operand of the same type: the node may answer the very value
///     it was given, and the operator takes no reference for it either. `reduce` is the shape -- it
///     answers its accumulator, so a `state::Ref` as its `initial` makes the node's value the
///     initial's box -- and it is the case the run-time skip was written for: without a hold here,
///     the hold the initial's own row gives back at the operator's position released the box while
///     the node's value still named it (`ref.reduce_takes_its_initial` answered "the value is not a
///     reference"). No operator can declare this one statically: whether the fold answers its
///     accumulator is its element function's answer, and the plan does not read that.
bool takesTheReferenceForItsValue(const GCGraph &graph, gc_node_ref_t at) {
    const GCNode *node = graph.node(at);
    if (node == nullptr || node->kind != GCNodeKind::Oper) {
        return false;
    }
    const camel::core::type::Type *type = node->dataType();
    if (type == nullptr || !isCountedHandle(type->code())) {
        return false;
    }
    const auto *body = graph.nodeBodyAs<GCOperBody>(at);
    if (body != nullptr &&
        camel::core::OperatorTable::instance().handsOnOperandValue(body->uri())) {
        return true;
    }
    for (const camel::execute::ExecutionPredecessor &predecessor :
         camel::execute::executionPredecessorsOf(graph, at)) {
        if (predecessor.kind != camel::execute::ExecutionPredecessor::Kind::Data) {
            continue;
        }
        const GCNode *operand = graph.node(predecessor.node);
        const camel::core::type::Type *operandType =
            operand != nullptr ? operand->dataType() : nullptr;
        if (operandType != nullptr && operandType->code() == type->code()) {
            return true;
        }
    }
    return false;
}

/// Whether the count applies to the value the node makes: its type is the reference handle the
/// runtime counts (`countedHandleCode`). The plan lays the rows of such a value out where a back
/// end runs them; every other value's rows keep the position the last-use column gives them,
/// because no back end can tell whether they ran -- `retainCountNow` and `giveBackCountNow` answer
/// false for a value that is not the handle, so a row for any other value is inert wherever it is
/// written.
bool valueIsCounted(const GCGraph &graph, gc_node_ref_t value) noexcept {
    const GCNode *node = graph.node(value);
    if (node == nullptr) {
        return false;
    }
    const camel::core::type::Type *type = node->dataType();
    if (type == nullptr) {
        return false;
    }
    return isCountedHandle(type->code());
}

/// The references that come back and the holds they are balanced by, as rows of the plan. The rule
/// is written out in the header; the placement of the rows is what the last-use column is for: a
/// reference packed into another value comes back where that value's own last use is, so the column
/// is the one answer both tables read.
void planRefRows(const GCGraph &graph, ExecPlan &plan, const ValueUses &uses) {
    const size_t count        = plan.topo.size();
    const size_t nodeCount    = graph.nodeCount();
    const auto &carried       = uses.carried;
    const auto &usesAt        = uses.usesAt;
    const auto &keptByProgram = uses.keptByProgram;
    plan.refHoldsAt.assign(count, 0);
    plan.refProducedAt.assign(count, 0);
    plan.refTakesAt.assign(count, 0);
    plan.refKeptAt.assign(count, 0);
    plan.refGiveBackOffsets.assign(count + 1, 0);
    plan.refEntryOffsets.assign(count + 1, 0);
    plan.refEntries.clear();
    plan.refGiveBacks.clear();

    // The rows, in topo order so that the table is a function of the graph and nothing else.
    std::vector<std::vector<ExecPlan::RefGiveBack>> rows(count);
    // The holds, one per reference taken beyond a value's own, by the position that takes it.
    std::vector<std::vector<gc_node_ref_t>> holds(count);
    for (size_t idx = 0; idx < count; ++idx) {
        const gc_node_ref_t value = plan.topo[idx];
        if (value >= nodeCount) {
            continue; // a node the plan does not run
        }
        // The values the table carries: `{its own}` for a producer, the union of its inputs for a
        // carrier (a JOIN, a GATE, a port, `:enum/field`, `:op/idx_arr`). A carrier's own value can
        // still be a counted handle that is a further holder of what it handed on
        // (`takesTheReferenceForItsValue`): `carried` says whose *uses* these are, and that is the
        // array for an element read, but the read holds the box too and the plan is where that
        // reference is taken. So such a value has rows of its own, laid the same way a producer's
        // are and with one hold more: its own, which its operator did not take.
        const bool made    = !carried[value].empty() && carried[value].front() == value;
        const bool takesIt = takesTheReferenceForItsValue(graph, value);
        if (!made && !takesIt) {
            continue; // a forwarder, a port, a constant, or a value nothing counts
        }
        // Where the reference of one use comes back: the use's own position, or -- when the user
        // packs its inputs -- where the value it made is last read, which is later than the use.
        // The loop rule is asked about that position, because that is the one the row runs at.
        // `nullopt` says the reference does not come back in this graph at all: the packing value
        // it was packed into is read by a node that passes it on -- a call hands it to the callee,
        // a field access writes the field into its own slot (`passesWhatItReadsOn`) -- so the
        // packing value does not die there and the value is one the program keeps.
        const auto landingOf =
            [&](uint32_t usePosition) -> std::optional<std::pair<uint32_t, bool>> {
            const gc_node_ref_t user = plan.topo[usePosition];
            if (!packsItsInputs(graph, user)) {
                return std::pair<uint32_t, bool>{usePosition, false};
            }
            const size_t packerPos =
                user < plan.topoIndexByRef.size() ? plan.topoIndexByRef[user] : count;
            uint32_t end = packerPos < plan.lastUseAt.size() ? plan.lastUseAt[packerPos]
                                                             : ExecPlan::kNoLastUse;
            if (end == ExecPlan::kNoLastUse || end >= count) {
                end = static_cast<uint32_t>(std::min<size_t>(packerPos, count - 1));
            } else if (passesWhatItReadsOn(graph, plan.topo[end], user, carried)) {
                return std::nullopt;
            }
            return std::pair<uint32_t, bool>{end, true};
        };
        // The uses of the value. The table names them for a value it carries; for a value only the
        // reader makes (a further holder) the uses are the nodes that read what it handed on, and
        // they are read off the same edges the walk above reads.
        std::vector<uint32_t> usePositions = usesAt[value];
        if (!made) {
            for (size_t reader = 0; reader < count; ++reader) {
                const gc_node_ref_t user = plan.topo[reader];
                if (user >= nodeCount) {
                    continue;
                }
                for (const camel::execute::ExecutionPredecessor &predecessor :
                     camel::execute::executionPredecessorsOf(graph, user)) {
                    if (predecessor.kind == camel::execute::ExecutionPredecessor::Kind::Data &&
                        predecessor.node == value) {
                        usePositions.push_back(static_cast<uint32_t>(reader));
                        break;
                    }
                }
            }
        }
        const uint32_t uses = static_cast<uint32_t>(usePositions.size());
        // Where a row is written: the position it is due at, and never any other. A give-back is
        // due at the node whose use of the value it comes back for (the use's landing position: its
        // own position, or -- when the user packs its inputs -- the last use of the value it packed
        // them into), and the reference of a value nothing reads is due at the node that made it.
        // That position is the node's own place in the plan, and every back end runs a node's rows
        // when that node has run (`applyRefEntriesForNode`, one hook, `callOperator` for the
        // operator kind and the node's own execution for the others). So the edge that makes a node
        // read the value is also what makes the row run after that read.
        //
        // The rule used to be different: a row due at a node that is not an operator moved forward
        // to the next operator of the same control region, and "later is the safe direction". That
        // is only true in one linear order. The plan has no edge between a position and the next
        // position, so a parallel back end (taskflow only connects what the graph connects) may run
        // the receiving operator before the use it was moved from, and the reference comes back
        // while a reader is still live (author review 2026-11-17, the rework of this function).
        // The same argument is why the holds are taken at the node that made the value and not at
        // the first position that writes one of its rows: only the producing node has an edge to
        // every reader, so only there is the hold taken before all of them.
        const bool counted = valueIsCounted(graph, value);
        // The rows written, and the holds taken beyond the value's own reference -- the two numbers
        // `1 + copies == give-backs + kept` is read from.
        uint32_t held   = 0;
        uint32_t copies = 0;
        // Set when a use's reference leaves this graph instead of coming back here (a packing
        // value a call is handed, or one a field access unpacks): the value is then one the program
        // keeps, exactly as a packing value that is the graph's result makes it kept, and the row
        // is not laid at a position where nothing dies.
        bool leavesTheGraph = false;
        for (const uint32_t usePosition : usePositions) {
            const auto landing = landingOf(usePosition);
            if (!landing) {
                leavesTheGraph = true;
                continue;
            }
            const auto [landingAt, packed] = *landing;
            // A use the plan runs more than once while the value it reads is made outside that
            // repetition cannot have its row at the use: the round would run it again and give the
            // reference back at the end of the first round. It is due at the exit of the outermost
            // loop that repeats over it (`loopRepeatingOver`), which is the one position of the
            // plan that runs once when the reading stops. One row per use, because each use holds
            // a reference of its own: two loops that each read the value give back at their own
            // exits, and neither one's exit is the other's.
            const auto exit =
                counted ? loopRepeatingOver(plan, idx, landingAt) : std::optional<uint32_t>{};
            const uint32_t at = exit ? *exit : landingAt;
            rows[at].push_back(
                ExecPlan::RefGiveBack{
                    .value  = value,
                    .at     = plan.topo[at],
                    .count  = 1,
                    .packed = packed});
            ++held;
        }
        if (uses == 0 && keptByProgram[value] == 0) {
            // Born with a reference nothing reads: it comes back as soon as the value exists, at
            // the node that made it.
            rows[idx].push_back(
                ExecPlan::RefGiveBack{.value = value, .at = plan.topo[idx], .count = 1});
            ++held;
        }
        const bool kept         = keptByProgram[value] != 0 || leavesTheGraph;
        // A further holder's value is not born with a reference the operator took: the plan takes
        // it, at this node's position, and `refTakesAt` is that answer (`refProducedAt` keeps the
        // one it means -- a value made with a reference of its own -- and `rowsOfValueAreExecuted`
        // admits both, because both have rows that run).
        if (takesIt) {
            plan.refTakesAt[idx] = 1;
        } else {
            plan.refProducedAt[idx] = 1;
        }
        plan.refKeptAt[idx]     = kept ? 1 : 0;
        // The holds the plan takes, beyond the reference a value is born with: one for each
        // reference it gives back, less that own reference, and one more when the graph keeps it.
        // Every row is written now -- there is no position that cannot take one -- so this is
        // `uses - 1` again, and a loop's exits are counted one each because each of them gives one
        // back. A further holder adds one: the hold the operator did not take is the plan's, and it
        // is what `2746e4025` was missing -- with no hold at the read, the release the array's own
        // hold is made of took the box to zero while the value the read handed on still named it.
        copies = static_cast<uint32_t>(held + (kept ? 1u : 0u) + (takesIt ? 1u : 0u)) - 1u;
        for (uint32_t take = 0; take < copies; ++take) {
            holds[idx].push_back(value);
        }
        plan.refHoldsAt[idx] += copies;
    }
    plan.refGiveBackAt.assign(count, 0);
    for (size_t idx = 0; idx < count; ++idx) {
        plan.refGiveBackOffsets[idx] = static_cast<uint32_t>(plan.refGiveBacks.size());
        for (const ExecPlan::RefGiveBack &row : rows[idx]) {
            plan.refGiveBacks.push_back(row);
            // The same schedule as a per-position count: what an instruction emitted at this
            // position would do -- the holds taken are the table's copies, these are the
            // give-backs.
            plan.refGiveBackAt[idx] += row.count;
        }
    }
    plan.refGiveBackOffsets[count] = static_cast<uint32_t>(plan.refGiveBacks.size());

    // The same schedule as the entries a back end reads, one row per thing to do. The holds are the
    // references beyond a value's own that the position takes, the give-backs the rows just laid
    // out, in the same order at the same positions.
    plan.refEntryOffsets[0] = 0;
    for (size_t idx = 0; idx < count; ++idx) {
        for (const gc_node_ref_t value : holds[idx]) {
            plan.refEntries.push_back(
                ExecPlan::RefEntry{
                    .kind  = ExecPlan::RefEntryKind::Retain,
                    .value = value,
                    .count = 1});
        }
        for (const ExecPlan::RefGiveBack &row : plan.refGiveBacksAt(idx)) {
            plan.refEntries.push_back(
                ExecPlan::RefEntry{
                    .kind   = ExecPlan::RefEntryKind::GiveBack,
                    .value  = row.value,
                    .count  = row.count,
                    .packed = row.packed});
        }
        plan.refEntryOffsets[idx + 1] = static_cast<uint32_t>(plan.refEntries.size());
    }

    // The slot-to-node half of the same table, for the back ends that run bytecode: an instruction
    // has a destination slot where the node form has the node. The entry is the node whose rows run
    // where the instruction that writes `slot` runs, so it is filled for the kinds that have an
    // instruction of their own writing their own slot -- the operator, the two calls, a FILL, a
    // COPY, an ACCS and a JOIN -- and not for a port or a GATE: both only carry another node's slot
    // (the bytecode compile gives a GATE its producer's pc and compiles a PORT into nothing), so a
    // lookup that found one of them would apply a row at the wrong place. Built once with the rows,
    // and only for the positions that have one, so the run pays one array read on the operator's
    // path and an empty table everywhere else.
    const auto runsItsOwnInstruction = [](GCNodeKind kind) {
        switch (kind) {
        case GCNodeKind::Oper:
        case GCNodeKind::Call:
        case GCNodeKind::Func:
        case GCNodeKind::Fill:
        case GCNodeKind::Copy:
        case GCNodeKind::Cast:
        case GCNodeKind::Accs:
        case GCNodeKind::Join:
            return true;
        default:
            return false;
        }
    };
    gc_data_idx_t widest = 0;
    for (size_t idx = 0; idx < count; ++idx) {
        if (plan.refEntryOffsets[idx] == plan.refEntryOffsets[idx + 1]) {
            continue;
        }
        const GCNode *node = graph.node(plan.topo[idx]);
        if (node == nullptr || !runsItsOwnInstruction(node->kind) || node->dataIndex <= 0) {
            continue;
        }
        widest = std::max(widest, node->dataIndex);
    }
    plan.refRowNodeBySlot.assign(
        static_cast<size_t>(widest) + 1,
        static_cast<uint32_t>(kInvalidNodeRef));
    for (size_t idx = 0; idx < count; ++idx) {
        if (plan.refEntryOffsets[idx] == plan.refEntryOffsets[idx + 1]) {
            continue;
        }
        const GCNode *node = graph.node(plan.topo[idx]);
        if (node == nullptr || !runsItsOwnInstruction(node->kind) || node->dataIndex <= 0) {
            continue;
        }
        plan.refRowNodeBySlot[static_cast<size_t>(node->dataIndex)] = plan.topo[idx];
    }
}

} // namespace

std::span<const ExecPlan::RefEntry> ExecPlan::refEntriesAt(size_t topoIndex) const {
    if (topoIndex + 1 >= refEntryOffsets.size()) {
        return {};
    }
    return {
        refEntries.data() + refEntryOffsets[topoIndex],
        refEntryOffsets[topoIndex + 1] - refEntryOffsets[topoIndex]};
}

size_t ExecPlan::refEntriesOfKind(RefEntryKind kind) const {
    size_t total = 0;
    for (const RefEntry &entry : refEntries) {
        if (entry.kind == kind) {
            total += entry.count;
        }
    }
    return total;
}

bool ExecPlan::refEntriesBalance() const {
    return refProducedTotal() + refEntriesOfKind(RefEntryKind::Retain) ==
           refEntriesOfKind(RefEntryKind::GiveBack) + refKeptTotal();
}

namespace {

/// The type code of the reference handle the runtime counts (`state::Ref`, camel/core/type/ref.h).
/// A slot of this type is one this count owns; a python object, a file handle and every other
/// foreign resource is a different code and keeps the lifetime it has today.
///
/// It is the handle type's own code and not `TypeCode::Ref`: that code is the compile-time
/// reference the AST carries (`RefData`, camel/core/data/ref.h), no value of a graph has it, and
/// comparing against it would skip every row and wire nothing.
camel::core::type::TypeCode countedHandleCode() {
    static const camel::core::type::TypeCode code = camel::core::type::RefType::get()->code();
    return code;
}

/// Whether a value of this type code is one the count owns: the reference handle the runtime counts
/// (`state::Ref`), asked once here and by nobody else.
///
/// One answer, asked by both sides of the same table: the plan, which lays the rows only where a
/// back end runs them (`valueIsCounted`), and the run, which applies a row only where the slot is
/// one (`applyRefEntriesAtPosition`). It was written three times in this file -- as
/// `TypeCode::Ref || countedHandleCode()` at each site -- and one of the three would have changed
/// first (author review 2026-11-17, item 9).
///
/// The `counted` flag this asks about has exactly one declaration, and it is not here: the module
/// that owns the type declares it on the descriptor (`ForeignResourceDescriptor::counted`,
/// modules/state/module.cpp `refDescriptor`), and `countedBlockOf`/`retainCountNow`/
/// `giveBackCountNow` are the place it is read -- per *value*, at run time, which is the only
/// moment a descriptor exists. The plan has a *type* and no value: what it can ask is the type code
/// the descriptor is declared for, which is what this function is, so the two are one fact read at
/// the two moments it is available and not two answers that could disagree.
bool isCountedHandle(camel::core::type::TypeCode code) noexcept {
    return code == countedHandleCode();
}

/// Whether the plan says a back end runs the rows of `value`: the value is one the plan counts, so
/// it has rows to run. The kind of node that makes it is not part of the answer: a CALL and a FILL
/// make a value as an operator does, and every back end runs their position's rows where it runs
/// the node (camel/execute/operator_call.h, `runNode`; exec_plan.cpp, `planRefRows`). The question
/// this must *not* ask is the producing node's kind -- that was the leak of 2026-11-16: the rows of
/// a box a call made were in the table, the balance read them, `found` counted the box, and no back
/// end ever applied them.
///
/// Two columns answer it and they are the two ways a value comes into this table: `refProducedAt`,
/// a value made with a reference of its own, and `refTakesAt`, a value that is a further holder of
/// an object an operand already holds and whose reference the plan takes. Both have rows; asking
/// only the first would leave a further holder's hold and its give-back in the table and no back end
/// applying them, which is the same leak from the other side.
bool rowsOfValueAreExecuted(const ExecPlan &plan, gc_node_ref_t value) noexcept {
    if (plan.graph == nullptr || value >= plan.topoIndexByRef.size()) {
        return false;
    }
    const size_t position = plan.topoIndexByRef[value];
    if (position == ExecPlan::kNotInPlan || position >= plan.refProducedAt.size()) {
        return false;
    }
    return plan.refProducedAt[position] != 0 || plan.refTakesAt[position] != 0;
}

/// The rows of one position of `topo`: the holds a value taken at that position and the references
/// that come back there. `applyRefEntriesForNode` is the published entry point; this is the work,
/// and it is one function so that a back end holding a slot and a back end holding a node cannot
/// drift apart in what they apply.
void applyRefEntriesAtPosition(
    const ExecPlan &plan, camel::core::context::Frame *frame, size_t position,
    slot_t produced) noexcept {
    if (frame == nullptr || plan.graph == nullptr) {
        return;
    }
    const std::span<const ExecPlan::RefEntry> entries = plan.refEntriesAt(position);
    if (entries.empty()) {
        return;
    }
    for (const ExecPlan::RefEntry &entry : entries) {
        if (!rowsOfValueAreExecuted(plan, entry.value)) {
            continue;
        }
        if (entry.value >= plan.dataIndexByRef.size()) {
            continue;
        }
        const gc_data_idx_t slot = plan.dataIndexByRef[entry.value];
        if (slot == 0) {
            continue; // the value has no slot: a node whose type is void counted nothing
        }
        if (!isCountedHandle(frame->codeAt(slot))) {
            continue;
        }
        // The node's own value is the one the operator just made: a back end that runs bytecode
        // has not published it into its slot yet (it does so after this returns), so the caller
        // passes it in.
        const bool ownValue = entry.value < plan.topoIndexByRef.size() &&
                              plan.topoIndexByRef[entry.value] == position;
        const slot_t value  = ownValue ? produced : frame->get<slot_t>(slot);
        camel::core::rtdata::Object *object =
            camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(value);
        if (object == nullptr) {
            continue;
        }
        for (uint32_t taken = 0; taken < entry.count; ++taken) {
            if (entry.kind == ExecPlan::RefEntryKind::Retain) {
                camel::core::rtdata::retainCountNow(object);
            } else {
                camel::core::rtdata::giveBackCountNow(object);
            }
        }
    }
}

} // namespace

void applyRefEntriesForNode(
    const ExecPlan &plan, camel::core::context::Frame *frame, gc_node_ref_t node,
    slot_t produced) noexcept {
    if (node >= plan.topoIndexByRef.size()) {
        return;
    }
    const size_t position = plan.topoIndexByRef[node];
    if (position == ExecPlan::kNotInPlan) {
        return;
    }
    applyRefEntriesAtPosition(plan, frame, position, produced);
}

void applyRefEntriesForLoopExit(
    const ExecPlan &plan, camel::core::context::Frame *frame, size_t position) noexcept {
    if (frame == nullptr || plan.graph == nullptr || position >= plan.topo.size()) {
        return;
    }
    // The rows the rounds held a value for are laid at the LOOP node's own position, and this is
    // the only moment that position is run: `loop.position` runs every round, so the rows there
    // are not an operator's rows but the loop's own exit (exec_plan.cpp, `loopRepeatingOver`). The
    // LOOP node's value is the loop's state after the last round -- `loopEnd` has just brought it
    // up to date -- so it is read from the frame here, not passed in as a call's result.
    const gc_node_ref_t node = plan.topo[position];
    const gc_data_idx_t slot = node < plan.dataIndexByRef.size() ? plan.dataIndexByRef[node] : 0;
    const slot_t own         = slot != 0 ? frame->get<slot_t>(slot) : NullSlot;
    applyRefEntriesAtPosition(plan, frame, position, own);
}

void applyRefEntriesOfOpenLoops(camel::core::context::Frame *frame) noexcept {
    if (frame == nullptr) {
        return;
    }
    const camel::runtime::GCGraph *graph = frame->graph();
    if (graph == nullptr) {
        return;
    }
    const ExecPlan &plan = graph->execPlan();
    for (const ExecLoop &loop : plan.loops) {
        if (loop.roundSlot == 0) {
            continue;
        }
        // -1 is what the loop's own test writes when it stops: `loopEnd`'s leaving branch has
        // already run this loop's rows, and running them a second time would take a hold the run
        // never took.
        const int64_t round =
            camel::core::rtdata::fromSlot<int64_t>(frame->get<slot_t>(loop.roundSlot));
        if (round < 0) {
            continue;
        }
        applyRefEntriesForLoopExit(plan, frame, loop.position);
        // The loop is no longer one the run is inside: the rows above are the ones its exit owed,
        // and a second call on this frame -- one back end's fault path is reached from another's
        // -- must not take them again (a second give-back would release a box the run still holds).
        // The value is the one `loopTest` writes when its test stops the loop, and this frame is on
        // its way back, so nothing reads it afterwards.
        frame->set(loop.roundSlot, camel::core::rtdata::toSlot(int64_t{-1}));
    }
}

size_t ExecPlan::refProducedTotal() const {
    return static_cast<size_t>(std::count(refProducedAt.begin(), refProducedAt.end(), uint8_t{1}));
}

size_t ExecPlan::refCopiesTotal() const {
    // One table of the holds (`refHoldsAt`), read as its total: the number of `Retain` rows is the
    // same number, because a hold and the row that takes it are written together.
    return static_cast<size_t>(std::accumulate(refHoldsAt.begin(), refHoldsAt.end(), uint64_t{0}));
}

size_t ExecPlan::refGiveBacksTotal() const {
    size_t total = 0;
    for (const RefGiveBack &row : refGiveBacks) {
        total += row.packed ? 0 : row.count;
    }
    return total;
}

size_t ExecPlan::refPackedTotal() const {
    size_t total = 0;
    for (const RefGiveBack &row : refGiveBacks) {
        total += row.packed ? row.count : 0;
    }
    return total;
}

size_t ExecPlan::refKeptTotal() const {
    return static_cast<size_t>(std::count(refKeptAt.begin(), refKeptAt.end(), uint8_t{1}));
}

bool ExecPlan::refBalanceHolds() const {
    return refProducedTotal() + refCopiesTotal() ==
           refGiveBacksTotal() + refKeptTotal() + refPackedTotal();
}

size_t ExecPlan::storageReuseCount(StorageReuse decision) const {
    return static_cast<size_t>(std::count(storageReuseAt.begin(), storageReuseAt.end(), decision));
}

std::string_view ExecPlan::operatorUriFor(gc_node_ref_t ref) const {
    const auto *body = graph != nullptr ? graph->nodeBodyAs<GCOperBody>(ref) : nullptr;
    if (body == nullptr) {
        return {};
    }
    const std::string_view declared = body->uri();
    if (storageReuseOf(ref) != StorageReuse::Copy) {
        return declared;
    }
    const auto plain = camel::core::OperatorTable::instance().plainForm(declared);
    return plain.has_value() ? *plain : declared;
}

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
    plan->tailValueRef =
        hasValueReturn ? camel::execute::resolveRuntimeTailValueRef(mutableGraph) : kInvalidNodeRef;
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
    const bool anchorOk =
        plan->tailValueTopoIndex < count && camel::execute::hasOnlyTrivialRuntimeTailSuffixAfter(
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
        plan->directCallArgOffsets[idx]  = static_cast<uint32_t>(plan->directCallArgSlots.size());
        plan->branchArmEntryOffsets[idx] = static_cast<uint32_t>(plan->branchArmEntries.size());
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
        // A call that writes captured variables hands on a tuple of the result and their new
        // versions, which are the values of its With inputs: it is never a tail call, whose value
        // the caller takes for the callee's own.
        const bool writesCaptures = node != nullptr && funcWriteMaskOf(graph, ref) != 0;
        plan->directCallTailEligible[idx] =
            anchorOk && (ref == plan->tailValueRef) && !writesCaptures;
        plan->directCallFeedsTailJoin[idx] =
            !writesCaptures && std::find(tailJoinArmValues.begin(), tailJoinArmValues.end(), ref) !=
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
    // The storage plan and the references that come back are one walk now (the author's ruling of
    // 2026-11-15: the last use is computed by the storage walk, not written twice), so the second
    // table is filled from inside the first and has no call of its own here.
    planStorageReuse(graph, *plan);

    for (gc_node_ref_t portRef : graph.normPorts()) {
        plan->calleePortSlots.push_back(dataIndexOf(graph, portRef));
    }
    for (gc_node_ref_t portRef : graph.withPorts()) {
        plan->calleePortSlots.push_back(dataIndexOf(graph, portRef));
    }
    return plan;
}

void rebuildLoopTablesForTests(const GCGraph &graph, ExecPlan &plan) {
    // What buildLoopTables appends to, given back first: the tables are a function of the order, so
    // a second call on a reordered plan must not find the first call's rows beside its own.
    plan.loops.clear();
    plan.loopSlots.clear();
    plan.loopEvents.clear();
    plan.loopEventOffsets.clear();
    plan.elidedAt.clear();
    buildLoopTables(graph, plan);
}

void noteCountedCreationForNode(const void *graph, gc_node_ref_t node, uint64_t boxes) noexcept {
    if (graph == nullptr || boxes == 0) {
        return;
    }
    const auto *version  = static_cast<const GCGraph *>(graph);
    const ExecPlan &plan = version->execPlan();
    // A node the plan names no position for is not a producing row either: the same answer an
    // unknown pc gets, and not an excuse to skip the question.
    const size_t position =
        node < plan.topoIndexByRef.size() ? plan.topoIndexByRef[node] : plan.topo.size();
    const bool named = position < plan.refProducedAt.size() && plan.refProducedAt[position] != 0;
    camel::core::rtdata::recordCountedCreation(named, boxes);
}

void noteCountedCreationAt(const void *graph, size_t pc, uint64_t boxes) noexcept {
    if (graph == nullptr || boxes == 0) {
        return;
    }
    const ExecPlan &plan = static_cast<const GCGraph *>(graph)->execPlan();
    gc_node_ref_t node   = kInvalidNodeRef;
    if (pc >= plan.countedSites.entryPc) {
        const auto row = plan.countedSites.nodeAtPc.find(pc - plan.countedSites.entryPc);
        if (row != plan.countedSites.nodeAtPc.end()) {
            node = row->second;
        }
    }
    noteCountedCreationForNode(graph, node, boxes);
}

} // namespace camel::runtime
