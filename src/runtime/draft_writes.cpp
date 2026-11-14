// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * In-place writes on a graph draft (see draft_writes.h), and the dead-write judgement asked of
 * either side of a commit (see effect_liveness.h).
 */

#include "camel/runtime/draft_writes.h"

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
        const GCGraph *callee = draft.calleeGraphOf(user);
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
    std::string_view uri, const type::type_vec_t &with, const type::type_vec_t &norm) {
    OperQueryKey key{std::string(uri), with.size(), with};
    key.types.insert(key.types.end(), norm.begin(), norm.end());
    {
        std::lock_guard lock(operQueryMutex);
        if (const auto it = operQueryMemo().find(key); it != operQueryMemo().end()) {
            return it->second;
        }
    }
    type::FunctionType *answer = nullptr;
    if (const auto resolver = camel::core::OperatorTable::instance().resolver(uri)) {
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
    // The slot `id` reads: a GATE forwards the slot of its value input.
    for (size_t hops = 0; hops < draft.nodeSlotCount(); ++hops) {
        const DraftNodeHeader *h = draft.header(id);
        if (!h || h->kind != GCNodeKind::Gate || draft.normInputsOf(id).empty()) {
            break;
        }
        id = draft.normInputsOf(id).back();
    }
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
            parent[root(id)] = root(draft.normInputsOf(id).back());
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
/// `var` parameter or a capture), its closure nodes, and the result of each region. One spelling of
/// them for both representations of a graph; the region results are the one part the two answer
/// differently, so `regionIsResult` is what they pass in.
template <typename RegionIsResult>
bool namesOf(
    gc_node_ref_t ref, std::span<const gc_node_ref_t> normPorts,
    std::span<const gc_node_ref_t> withPorts, std::span<const gc_node_ref_t> closureNodes,
    gc_node_ref_t entry, gc_node_ref_t exit, gc_node_ref_t output, gc_node_ref_t result,
    RegionIsResult regionIsResult) {
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
    return regionIsResult(ref);
}

/// A frozen graph, read the way the graph space lays it out.
struct FrozenGraphView {
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
            });
    }
    const GCOperBody *bodyOf(gc_node_ref_t ref) const { return graph.nodeBodyAs<GCOperBody>(ref); }
    std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const {
        return graph.normInputsOf(ref);
    }
    bool isRead(gc_node_ref_t ref) const {
        return !graph.normUsersOf(ref).empty() || !graph.withUsersOf(ref).empty();
    }
};

/// A draft under rewrite, read the way the transaction holds it: the same questions, spelled the
/// way this side spells them (`header` and `payloadOf` where the frozen graph says `node` and
/// `nodeBodyAs`).
struct DraftGraphView {
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
    bool isRead(gc_node_ref_t ref) const {
        return !draft.normUsersOf(ref).empty() || !draft.withUsersOf(ref).empty();
    }
};

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
        if (writesOperandAt(*body, index) && view.isNamed(inputs[index])) {
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

bool deadWrite(const GraphDraft &draft, gc_node_ref_t ref) {
    return deadWriteWith(DraftGraphView{draft}, ref);
}

} // namespace camel::runtime