// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * In-place writes on a graph draft (see draft_writes.h), and the dead-write judgement asked of
 * either side of a commit (see effect_liveness.h).
 */

#include "camel/runtime/draft_writes.h"
#include "camel/core/operator_table.h"

#include "camel/core/context/context.h"
#include "camel/core/mm/alloc/growable.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/effect_liveness.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::runtime {

namespace type = camel::core::type;

namespace {

struct OperQueryKey {
    std::string uri;
    size_t withCount = 0;
    type::type_vec_t types; // with types, then norm types

    bool operator==(const OperQueryKey &other) const = default;
};

struct OperQueryKeyHash {
    size_t operator()(const OperQueryKey &key) const {
        size_t seed = std::hash<std::string>{}(key.uri) ^ (key.withCount * 0x9e3779b97f4a7c15ULL);
        for (const type::Type *t : key.types) {
            seed ^=
                std::hash<const void *>{}(t) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

std::mutex operQueryMutex;
std::unordered_map<OperQueryKey, type::FunctionType *, OperQueryKeyHash> &operQueryMemo() {
    static auto *memo =
        new std::unordered_map<OperQueryKey, type::FunctionType *, OperQueryKeyHash>();
    return *memo;
}

bool isVarParam(const type::FunctionType *funcType, bool isWith, size_t index) {
    if (!funcType) {
        return false;
    }
    return isWith ? index < funcType->withTypesCount() && funcType->withIsVarAt(index)
                  : index < funcType->normTypesCount() && funcType->normIsVarAt(index);
}

/// True when the argument at position `index` of `user`'s norm (or with) inputs is written by
/// `user`: for an OPER, what the operator's resource declaration says (design principles 3.3;
/// an operator without one is read conservatively, OperatorTable::writesOperand); for a FUNC,
/// a `var` parameter of the graph it calls; for a CALL, a `var` parameter of its callee's type.
/// Only `isWrittenInPlace` below asks it: the question is about one argument, the answer a pass
/// wants is about one value, so the finer one stays here rather than on the draft's surface.
bool isWrittenArgument(const GraphDraft &draft, gc_node_ref_t user, bool isWith, size_t index) {
    const DraftNodeHeader *h = draft.header(user);
    if (!h) {
        return false;
    }
    switch (h->kind) {
    case GCNodeKind::Oper: {
        const auto *body = reinterpret_cast<const GCOperBody *>(draft.payloadOf(user).data());
        // The operator's resource declaration is the truth (design principles 3.3); its
        // signature's `var` parameters are checked against it when it is registered.
        return camel::core::OperatorTable::instance().writesOperand(body->oper, isWith, index);
    }
    case GCNodeKind::Func: {
        const auto *callee = draft.calleeGraphOf(user);
        return callee && isVarParam(callee->funcType(), isWith, index);
    }
    case GCNodeKind::Loop:
        // The With inputs are the captures of the loop's graphs: a variable among them may be
        // written by the body. The Norm inputs (the array, the first values) are values.
        return isWith;
    case GCNodeKind::Call: {
        // The first with input is the callee; the others are its with arguments.
        const auto withInputs = draft.withInputsOf(user);
        if (withInputs.empty() || (isWith && index == 0)) {
            return false;
        }
        const DraftNodeHeader *callee = draft.header(withInputs.front());
        if (!callee || !callee->dataType || callee->dataType->code() != type::TypeCode::Function) {
            return false;
        }
        return isVarParam(
            static_cast<const type::FunctionType *>(callee->dataType),
            isWith,
            isWith ? index - 1 : index);
    }
    default:
        return false;
    }
}

} // namespace

type::FunctionType *operatorOverloadType(
    const std::shared_ptr<camel::core::context::Context> &context, std::string_view uri,
    const type::type_vec_t &with, const type::type_vec_t &norm) {
    OperQueryKey key{std::string(uri), with.size(), with};
    key.types.insert(key.types.end(), norm.begin(), norm.end());
    {
        std::lock_guard lock(operQueryMutex);
        if (const auto it = operQueryMemo().find(key); it != operQueryMemo().end()) {
            return it->second;
        }
    }
    type::FunctionType *answer = nullptr;
    if (const auto resolver =
            context != nullptr ? context->resolverOf(std::string(uri)) : nullptr) {
        try {
            const std::vector<type::StaticArg> statics(norm.size());
            const auto resolved = resolver->resolveWith(with, norm, statics);
            answer              = resolved ? *resolved : nullptr;
        } catch (const camel::core::mm::MemoryExhausted &) {
            throw; // not an answer about the overload
        } catch (const std::exception &) {
            answer = nullptr;
        }
    }
    std::lock_guard lock(operQueryMutex);
    return operQueryMemo().emplace(std::move(key), answer).first->second;
}

bool isWrittenInPlace(const GraphDraft &draft, gc_node_ref_t id) {
    // The slot `id` reads: the value behind the gates that forward it, taken by the one walk
    // (`camel/runtime/draft.h#ungated`: a gate's first ordinary input is the value, the inputs
    // after it are the reads of the region's writes).
    id = ungated(draft, id);
    // Every user of that slot, through the GATEs that forward it. A GATE reached along two
    // paths is looked at once: a shared DAG of GATEs would otherwise cost a walk per path.
    std::vector<gc_node_ref_t> work{id};
    std::unordered_set<gc_node_ref_t> seen{id};
    while (!work.empty()) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (!draft.header(n)) {
            continue;
        }
        for (const bool isWith : {false, true}) {
            for (gc_node_ref_t user : isWith ? draft.withUsersOf(n) : draft.normUsersOf(n)) {
                const DraftNodeHeader *h = draft.header(user);
                if (!h) {
                    continue;
                }
                if (h->kind == GCNodeKind::Gate) {
                    if (seen.insert(user).second) {
                        work.push_back(user);
                    }
                    continue;
                }
                const auto inputs = isWith ? draft.withInputsOf(user) : draft.normInputsOf(user);
                for (size_t i = 0; i < inputs.size(); ++i) {
                    if (inputs[i] == n && isWrittenArgument(draft, user, isWith, i)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

WrittenSlots::WrittenSlots(const GraphTransaction &transaction)
    : written_(transaction.graph().nodeSlotCount(), false) {
    const auto &draft = transaction.graph();
    // GATEs share the slot of the value they forward: union them, mark the slots written
    // through any argument, and read the answer per node.
    std::vector<gc_node_ref_t> parent(written_.size());
    for (gc_node_ref_t id = 0; id < parent.size(); ++id) {
        parent[id] = id;
    }
    const auto root = [&](gc_node_ref_t id) {
        while (parent[id] != id) {
            parent[id] = parent[parent[id]];
            id         = parent[id];
        }
        return id;
    };
    for (gc_node_ref_t id = 0; id < parent.size(); ++id) {
        const auto *h = draft.header(id);
        if (h && h->kind == GCNodeKind::Gate && !draft.normInputsOf(id).empty()) {
            // A GATE belongs with the value it forwards: its first normal input.
            parent[root(id)] = root(draft.normInputsOf(id).front());
        }
    }
    for (gc_node_ref_t id = 0; id < parent.size(); ++id) {
        if (!draft.header(id)) {
            continue;
        }
        for (const bool isWith : {false, true}) {
            const auto inputs = isWith ? draft.withInputsOf(id) : draft.normInputsOf(id);
            for (size_t i = 0; i < inputs.size(); ++i) {
                if (isWrittenArgument(draft, id, isWith, i)) {
                    written_[root(inputs[i])] = true;
                }
            }
        }
    }
    for (gc_node_ref_t id = 0; id < parent.size(); ++id) {
        written_[id] = written_[root(id)];
    }
}

// ---- The dead-write judgement, both sides of a commit ---------------------------------------
//
// One reading of one rule (design principles 9.7): "written in place" above says where a write
// goes; these say whether the write itself is still anybody's. It lives in this file because the
// question is asked twice about the same graph -- once on the draft a rule is admitted on, once on
// the frozen graph the pass boundary checks -- and the two must not be two readings. The
// operator-level half
// (`isWriteOfProgramState`, `writesProgramState`: what counts as a write) stays in effects.cpp.

namespace {

/// Whether the declaration says the operator writes the operand at `index`.
bool writesOperandAt(const GCOperBody &body, size_t index) {
    return camel::core::OperatorTable::instance().writesOperand(body.oper, false, index);
}

/// The names a graph keeps: its entry, its exit, the node whose value it returns, its ports (a
/// `var` parameter or a capture), its closure nodes, the result of each region, and the ports a
/// control region is entered with. One spelling of them for both representations of a graph; the
/// regions are the one part the two read differently, so `regionIsResult` and `regionNamesPort` are
/// what they pass in.
///
/// A control region's ports belong to this list because the table is positional and the table is
/// how the region is entered: `LoopView::bodyStateParam(j)` and `condStateParam(j)` are the
/// j-th entry, and the j-th state is that value. A port node an erasure took leaves the entry
/// pointing at nothing, and a table that dropped the entry instead renames every state after it --
/// two wrong answers to one question. `validateRuntimeRegions` refuses the first (a control region
/// needs every port it lists), so an unread port of a control region is a name and not dead code.
/// An annotation region is not in this: a rewrite may remove one of its ports, and the published
/// graph then holds the entry as "none".
template <typename RegionIsResult, typename RegionNamesPort>
bool namesOf(
    gc_node_ref_t ref, std::span<const gc_node_ref_t> normPorts,
    std::span<const gc_node_ref_t> withPorts, std::span<const gc_node_ref_t> closureNodes,
    gc_node_ref_t entry, gc_node_ref_t exit, gc_node_ref_t output, gc_node_ref_t result,
    RegionIsResult regionIsResult, RegionNamesPort regionNamesPort) {
    if (ref == kInvalidNodeRef) {
        return false;
    }
    if (ref == entry || ref == exit || ref == output || ref == result) {
        return true;
    }
    const auto names = [ref](std::span<const gc_node_ref_t> values) {
        return std::find(values.begin(), values.end(), ref) != values.end();
    };
    if (names(normPorts) || names(withPorts) || names(closureNodes)) {
        return true;
    }
    return regionIsResult(ref) || regionNamesPort(ref);
}

/// Whether a region of `kind` is one the graph enters with a port list: the condition and the body
/// of a LOOP, and the arm of a BRCH (see `namesOf`).
bool kindsRegionEntersWithPorts(GCRegionKind kind) {
    return kind == GCRegionKind::Arm || kind == GCRegionKind::LoopCond ||
           kind == GCRegionKind::LoopBody;
}

/// A frozen graph, read the way the graph space lays it out.
struct FrozenGraphView {
    explicit FrozenGraphView(const GCGraph &frozen) : graph(frozen) {}

    const GCGraph &graph;

    bool isNamed(gc_node_ref_t ref) const {
        if (ref == kInvalidNodeRef || !graph.containsNodeRef(ref)) {
            return false;
        }
        const GCNode *node = graph.node(ref);
        if (node == nullptr || node->kind == GCNodeKind::Dead) {
            return false;
        }
        return namesOf(
            ref,
            graph.normPorts(),
            graph.withPorts(),
            graph.closureNodes(),
            graph.entryNodeRef(),
            graph.exitNodeRef(),
            graph.outputNodeRef(),
            graph.returnNodeRef(),
            [this](gc_node_ref_t candidate) {
                for (uint32_t index = 0; index < graph.regionCount(); ++index) {
                    const GCRegion *region = graph.region(index);
                    if (region != nullptr && region->result == candidate) {
                        return true;
                    }
                }
                return false;
            },
            [this](gc_node_ref_t candidate) {
                for (uint32_t index = 0; index < graph.regionCount(); ++index) {
                    const GCRegion *region = graph.region(index);
                    if (region == nullptr || !kindsRegionEntersWithPorts(region->kind)) {
                        continue;
                    }
                    const auto ports = graph.regionPorts(index);
                    if (std::find(ports.begin(), ports.end(), candidate) != ports.end()) {
                        return true;
                    }
                }
                return false;
            });
    }
    const GCOperBody *bodyOf(gc_node_ref_t ref) const { return graph.nodeBodyAs<GCOperBody>(ref); }
    std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const {
        return graph.normInputsOf(ref);
    }
    /// Whether a value the write leaves is read by a reader the program can still see. A reader the
    /// live set does not reach -- a closure definition nothing calls -- reads a value nobody will
    /// ever look at; counting it is what kept a dead write alive, and it is why a graph whose only
    /// last reader was dropped would be refused by its own credential check with "has effect node
    /// N with neither a read of its value nor a place in its chain". The criterion, in one line: a
    /// write may be called dead when it is no root and every node that reads it lies outside this
    /// set. The set is asked here, once per view, from `camel::runtime::liveNodesOf` -- the same
    /// walk the draft face asks, one kernel behind both.
    bool isRead(gc_node_ref_t ref) const {
        const auto normUsers = graph.normUsersOf(ref);
        const auto withUsers = graph.withUsersOf(ref);
        if (normUsers.empty() && withUsers.empty()) {
            return false;
        }
        if (!live_.has_value()) {
            live_ = camel::runtime::liveNodesOf(graph);
        }
        auto isLive = [this](gc_node_ref_t user) {
            return std::binary_search(live_->begin(), live_->end(), user);
        };
        for (gc_node_ref_t user : normUsers) {
            if (isLive(user)) {
                return true;
            }
        }
        for (gc_node_ref_t user : withUsers) {
            if (isLive(user)) {
                return true;
            }
        }
        return false;
    }
    bool isGate(gc_node_ref_t ref) const {
        if (ref == kInvalidNodeRef || !graph.containsNodeRef(ref)) {
            return false;
        }
        const GCNode *node = graph.node(ref);
        return node != nullptr && node->kind == GCNodeKind::Gate;
    }

  private:
    /// The live set of this graph, asked once per view: the walk is `camel::runtime::liveNodesOf`,
    /// so this face and the draft face answer the same question from the same code.
    mutable std::optional<std::vector<gc_node_ref_t>> live_;
};

/// A draft under rewrite, read the way the transaction holds it: the same questions, spelled the
/// way this side spells them (`header` and `payloadOf` where the frozen graph says `node` and
/// `nodeBodyAs`).
struct DraftGraphView {
    explicit DraftGraphView(const GraphDraft &graph) : draft(graph) {}

    const GraphDraft &draft;

    bool isNamed(gc_node_ref_t ref) const {
        if (draft.header(ref) == nullptr || !draft.alive(ref)) {
            return false;
        }
        return namesOf(
            ref,
            draft.normPorts(),
            draft.withPorts(),
            draft.closureNodes(),
            draft.entryNode(),
            draft.exitNode(),
            draft.outputNode(),
            draft.returnNode(),
            [this](gc_node_ref_t candidate) {
                for (uint32_t index = 0; index < draft.regionCount(); ++index) {
                    if (draft.region(index).result == candidate) {
                        return true;
                    }
                }
                return false;
            },
            [this](gc_node_ref_t candidate) {
                for (uint32_t index = 0; index < draft.regionCount(); ++index) {
                    if (!kindsRegionEntersWithPorts(draft.region(index).kind)) {
                        continue;
                    }
                    const auto ports = draft.regionPortsOf(index);
                    if (std::find(ports.begin(), ports.end(), candidate) != ports.end()) {
                        return true;
                    }
                }
                return false;
            });
    }
    const GCOperBody *bodyOf(gc_node_ref_t ref) const {
        const std::span<const std::byte> payload = draft.payloadOf(ref);
        if (payload.size() < sizeof(GCOperBody)) {
            return nullptr;
        }
        return reinterpret_cast<const GCOperBody *>(payload.data());
    }
    std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const {
        return draft.normInputsOf(ref);
    }
    /// Whether a value the write leaves is read by a reader the program can still see. A reader
    /// inside a node the live set does not reach -- a closure definition nothing calls -- reads a
    /// value nobody will ever look at, and counting it keeps a dead write alive (design principles
    /// 9.7: the check is that the write is no root and its value unread). The set is asked here,
    /// not handed in: one draft, one set, cached on the view.
    bool isRead(gc_node_ref_t ref) const {
        const auto normUsers = draft.normUsersOf(ref);
        const auto withUsers = draft.withUsersOf(ref);
        if (normUsers.empty() && withUsers.empty()) {
            return false;
        }
        if (!live_.has_value()) {
            live_ = camel::runtime::liveNodesOf(draft);
        }
        auto isLive = [this](gc_node_ref_t user) {
            return std::binary_search(live_->begin(), live_->end(), user);
        };
        for (gc_node_ref_t user : normUsers) {
            if (isLive(user)) {
                return true;
            }
        }
        for (gc_node_ref_t user : withUsers) {
            if (isLive(user)) {
                return true;
            }
        }
        return false;
    }
    bool isGate(gc_node_ref_t ref) const {
        const DraftNodeHeader *header = draft.header(ref);
        return header != nullptr && header->kind == GCNodeKind::Gate;
    }

  private:
    /// The live set of this draft, asked once. The walk is `camel::runtime::liveNodesOf`, so this
    /// view and every other reader of the set answer from the same code.
    mutable std::optional<std::vector<gc_node_ref_t>> live_;
};

/// Whether a root holds the cell `ref` is. A GATE has no cell of its own -- it forwards the cell of
/// its value input, which is its first normal input, the same node `isWrittenInPlace` above follows
/// (design principles 3.1). So the cell a `var` parameter writes
/// is the caller's cell, not the gate that carries it across the call: asking the gate alone
/// answers "no root holds it" for every write made through a parameter, which is a write the caller
/// can still read (design principles 3.5, 9.7).
///
/// It follows `inputs.front()`, as `isWrittenInPlace` does: `inputs.back()` would answer about a
/// wait's cell.
template <typename View> bool placeIsRootedWith(const View &view, gc_node_ref_t ref) {
    std::unordered_set<gc_node_ref_t> seen;
    gc_node_ref_t place = ref;
    while (seen.insert(place).second) {
        if (view.isNamed(place)) {
            return true;
        }
        if (!view.isGate(place)) {
            return false;
        }
        const auto inputs = view.normInputsOf(place);
        if (inputs.empty()) {
            return false;
        }
        place = inputs.front();
    }
    // A cycle of GATEs: nothing behind it, so no root holds it.
    return false;
}

/// A root holds the value the write leaves, or the place it writes into.
template <typename View> bool writeIsRootedWith(const View &view, gc_node_ref_t ref) {
    if (view.isNamed(ref)) {
        return true;
    }
    const GCOperBody *body = view.bodyOf(ref);
    if (body == nullptr) {
        return false;
    }
    const auto inputs = view.normInputsOf(ref);
    for (size_t index = 0; index < inputs.size(); ++index) {
        if (writesOperandAt(*body, index) && placeIsRootedWith(view, inputs[index])) {
            return true;
        }
    }
    return false;
}

template <typename View> bool deadWriteWith(const View &view, gc_node_ref_t ref) {
    const GCOperBody *body = view.bodyOf(ref);
    if (body == nullptr) {
        return false;
    }
    if (!isWriteOfProgramState(*body, view.normInputsOf(ref).size())) {
        return false;
    }
    return !writeIsRootedWith(view, ref) && !view.isRead(ref);
}

} // namespace

bool valueIsRead(const GCGraph &graph, gc_node_ref_t ref) {
    return FrozenGraphView{graph}.isRead(ref);
}

bool writeIsRooted(const GCGraph &graph, gc_node_ref_t ref) {
    return writeIsRootedWith(FrozenGraphView{graph}, ref);
}

bool deadWrite(const GCGraph &graph, gc_node_ref_t ref) {
    return deadWriteWith(FrozenGraphView{graph}, ref);
}

bool writeIsRooted(const GraphDraft &draft, gc_node_ref_t ref) {
    return writeIsRootedWith(DraftGraphView{draft}, ref);
}

bool deadWriteOnceUnread(const GCGraph &graph, gc_node_ref_t ref) {
    const FrozenGraphView view{graph};
    const GCOperBody *body = view.bodyOf(ref);
    return body != nullptr && isWriteOfProgramState(*body, view.normInputsOf(ref).size()) &&
           !writeIsRootedWith(view, ref);
}

bool deadWriteOnceUnread(const GraphDraft &draft, gc_node_ref_t ref) {
    const DraftGraphView view{draft};
    const GCOperBody *body = view.bodyOf(ref);
    return body != nullptr && isWriteOfProgramState(*body, view.normInputsOf(ref).size()) &&
           !writeIsRootedWith(view, ref);
}

bool deadWrite(const GraphDraft &draft, gc_node_ref_t ref) {
    return deadWriteWith(DraftGraphView{draft}, ref);
}

bool GraphDraft::namesNode(gc_node_ref_t ref) const { return DraftGraphView{*this}.isNamed(ref); }

} // namespace camel::runtime
