// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Computation of the execution plan of a graph version (camel/runtime/exec_plan.h).
 */

#include "camel/runtime/exec_plan.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/profile/profile.h"
#include "camel/runtime/control_regions.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <unordered_set>

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

/// The operators that use a variable's cell without keeping the value in it: what a cell may be
/// used by and stay a variable (`:var/new`, `:var/get`, `:var/peek`, `:var/take`, the assignment
/// operators). Any other user -- a call handed the `var`, a capture port -- takes the cell out of
/// the graph where its value's observers can be seen.
bool isVariableOperator(std::string_view uri) {
    return uri.starts_with(":var/") || uri.starts_with(":op/assn");
}

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
    while (!pending.empty() && seen.size() < 256) {
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
    if (cell < cache.size()) {
        cache[cell] = escapes ? 2 : 1;
    }
    return escapes;
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
    const GCGraph &graph, const std::vector<std::vector<gc_node_ref_t>> &users, gc_node_ref_t cell) {
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
///   * otherwise the update reuses the input's storage (StorageReuse::Input).
///
/// A loop's state comes round to the next round (the LOOP node takes the value of the body), so
/// the walk runs twice: the second time, each loop's body starts from the state its end reached.
void planStorageReuse(const GCGraph &graph, ExecPlan &plan) {
    const size_t count = plan.topo.size();
    plan.storageReuseAt.assign(count, StorageReuse::None);
    const auto &table            = camel::core::OperatorTable::instance();
    const size_t nodeCount       = graph.nodeCount();
    std::vector<std::vector<gc_node_ref_t>> users(nodeCount);
    for (size_t idx = 0; idx < count; ++idx) {
        const gc_node_ref_t at = plan.topo[idx];
        for (const gc_node_ref_t in : graph.normInputsOf(at)) {
            if (in < users.size()) {
                users[in].push_back(at);
            }
        }
        for (const gc_node_ref_t in : graph.withInputsOf(at)) {
            if (in < users.size()) {
                users[in].push_back(at);
            }
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
                                decision = StorageReuse::Input;
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
}

} // namespace

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
    planStorageReuse(graph, *plan);

    for (gc_node_ref_t portRef : graph.normPorts()) {
        plan->calleePortSlots.push_back(dataIndexOf(graph, portRef));
    }
    for (gc_node_ref_t portRef : graph.withPorts()) {
        plan->calleePortSlots.push_back(dataIndexOf(graph, portRef));
    }
    return plan;
}

} // namespace camel::runtime
