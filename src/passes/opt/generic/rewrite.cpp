// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Draft-editing helpers shared by the generic optimization passes (see rewrite.h).
 */

#include "rewrite.h"
#include "camel/runtime/control.h"

#include "camel/core/operator_traits.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_writes.h"
#include "camel/utils/log.h"

#include "camel/core/type/intern.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <stdexcept>
#include <vector>

namespace camel::passes::generic {

using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::kInvalidNodeRef;

std::string_view operUriOf(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return {};
    }
    return reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data())->uri();
}

camel::core::OperId operIdOf(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return camel::core::kNoOper;
    }
    return reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data())->oper;
}

/// Whether the pass may treat a call of the operator as a value of its own: fold it, move it,
/// merge it with an identical call or drop it. An operator that must run where the program writes
/// it is not one, whatever its effect (a rule's value and an `analysis` are pure and unfoldable).
bool isPureOper(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return false;
    }
    const auto *body = reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data());
    return camel::core::OperatorTable::instance().mayBeRecomputed(body->oper);
}

bool isValueOnly(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h) {
        return false;
    }
    switch (h->kind) {
    case GCNodeKind::Data:
        return h->dataIndex < 0;
    case GCNodeKind::Cast:
    case GCNodeKind::Copy:
    case GCNodeKind::Accs:
    case GCNodeKind::Fill:
    case GCNodeKind::Oper:
        return camel::runtime::nodeIsFreelySchedulable(draft, id, true);
    default:
        return false;
    }
}

bool isDeadWhenUnread(const GraphDraft &draft, gc_node_ref_t id) {
    if (isValueOnly(draft, id)) {
        return true;
    }
    return camel::runtime::isUnanchoredPure(draft, id);
}

bool isReplaceable(const GraphDraft &draft, gc_node_ref_t id) {
    // The entry node is only a marker (erasing it resets it, and encoding then derives it from
    // the first node), so it does not pin a node; inlining often leaves it on a dead value.
    return id != draft.exitNode() && id != draft.outputNode() && id != draft.returnNode();
}

bool reaches(const GraphDraft &draft, gc_node_ref_t from, gc_node_ref_t to) {
    std::vector<bool> seen(draft.nodeSlotCount(), false);
    std::vector<gc_node_ref_t> work{from};
    while (!work.empty()) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (n == to) {
            return true;
        }
        if (seen[n]) {
            continue;
        }
        seen[n] = true;
        for (auto users : {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
            work.insert(work.end(), users.begin(), users.end());
        }
    }
    return false;
}

bool readsNoWrittenSlot(const GraphDraft &draft, gc_node_ref_t id) {
    std::vector<gc_node_ref_t> work{id};
    std::vector<bool> seen(draft.nodeSlotCount(), false);
    while (!work.empty()) {
        const gc_node_ref_t at = work.back();
        work.pop_back();
        if (at >= seen.size() || seen[at] || draft.header(at) == nullptr) {
            continue;
        }
        seen[at] = true;
        // A node written in place is not one value: the slot holds what each write put there, so a
        // computation that reads it is the value of one read, not of the graph.
        if (camel::runtime::isWrittenInPlace(draft, at)) {
            return false;
        }
        // The walk follows what the value is computed from: the node's own inputs, and the waits
        // that order them, because a node on the way may read the written slot itself.
        for (auto inputs :
             {draft.normInputsOf(at), draft.withInputsOf(at), draft.ctrlInputsOf(at),
              draft.syncInputsOf(at)}) {
            work.insert(work.end(), inputs.begin(), inputs.end());
        }
    }
    return true;
}

std::vector<gc_node_ref_t> orderedSources(const GraphDraft &draft, gc_node_ref_t id) {
    std::vector<gc_node_ref_t> sources, work;
    std::vector<bool> seen(draft.nodeSlotCount(), false);
    for (auto inputs : {draft.normInputsOf(id), draft.withInputsOf(id)}) {
        work.insert(work.end(), inputs.begin(), inputs.end());
    }
    while (!work.empty()) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (n >= seen.size() || seen[n] || !draft.header(n)) {
            continue;
        }
        seen[n] = true;
        if (carriesOrdering(draft, n, true)) {
            sources.push_back(n);
            continue;
        }
        // A pure node on a chain (a statement after an effect) runs after what its chain
        // predecessor runs after as well: the control inputs of the chain as it was written, and
        // the sync predecessor of the chain of a `sync` block.
        for (auto inputs :
             {draft.normInputsOf(n), draft.withInputsOf(n), draft.ctrlInputsOf(n),
              draft.syncInputsOf(n)}) {
            work.insert(work.end(), inputs.begin(), inputs.end());
        }
    }
    return sources;
}

void replaceNode(GraphDraft &draft, gc_node_ref_t id, gc_node_ref_t replacement) {
    const auto copy = [](std::span<const gc_node_ref_t> refs) {
        return std::vector<gc_node_ref_t>(refs.begin(), refs.end());
    };
    // Users keep running after what `id` ran after: its control predecessors, the statement it was
    // written after in its `sync` block, and the ordered nodes its value came from (a use of a
    // value implies its producer's ordering; a constant replacing it does not carry that).
    auto preds = copy(draft.ctrlInputsOf(id));
    for (gc_node_ref_t source : draft.syncInputsOf(id)) {
        if (std::ranges::find(preds, source) == preds.end()) {
            preds.push_back(source);
        }
    }
    for (gc_node_ref_t source : orderedSources(draft, id)) {
        if (std::ranges::find(preds, source) == preds.end()) {
            preds.push_back(source);
        }
    }
    preds = camel::runtime::controlFrontier(draft, preds);
    std::vector<gc_node_ref_t> users;
    for (auto group : {draft.normUsersOf(id), draft.withUsersOf(id), draft.ctrlUsersOf(id)}) {
        for (gc_node_ref_t u : group) {
            if (std::ranges::find(users, u) == users.end()) {
                users.push_back(u);
            }
        }
    }
    const auto ctrlUsers = copy(draft.ctrlUsersOf(id));
    // The statements written directly after the removed one keep their place in the chain: the
    // chain is an ordering of its own (docs/design/effects/edges.cn.md 1.5 and 4.6), so a successor
    // follows what the removed node followed rather than only being ordered by it. eraseNode closes
    // the chain over the removed node, with the control edge naming the same pair beside the sync
    // edge; this only has to know which users are already this node's successors in the chain.
    const auto syncUsers = copy(draft.syncUsersOf(id));
    // The waits the removed node carried through a chain, which its consumers keep in that kind
    // (`handOverSyncWait`).
    const auto chainWait = copy(draft.syncInputsOf(id));
    // The wait the removed node carried for its successor, when the chain does not hand one on.
    // A branch is not one of them: what waits on a BRCH is in one of its arms, and the successor
    // standing where the removed node stood keeps its own arm (src/runtime/branch_arms.cpp). The
    // wait is read in either kind: a node of a `sync` block waits through the sync edge as well as
    // through the control edge beside it, and the successor has to keep that wait whichever kind
    // named it (docs/design/effects/edges.cn.md 1, 4.6).
    std::vector<gc_node_ref_t> inheritedWait;
    for (auto waits : {draft.ctrlInputsOf(id), draft.syncInputsOf(id)}) {
        for (gc_node_ref_t pred : waits) {
            const camel::runtime::DraftNodeHeader *header = draft.header(pred);
            if (header != nullptr && header->kind == GCNodeKind::Brch) {
                continue;
            }
            if (std::ranges::find(inheritedWait, pred) == inheritedWait.end()) {
                inheritedWait.push_back(pred);
            }
        }
    }

    if (replacement != kInvalidNodeRef) {
        draft.replaceAllValueUses(id, replacement);
        // A value computed elsewhere (a constant, a forwarded input) now stands for this node's
        // source: diagnostics on it point where the replaced node came from.
        if (draft.nodeOrigin(replacement) == 0) {
            draft.setNodeOrigin(replacement, draft.nodeOrigin(id));
        }
    }
    for (gc_node_ref_t user : ctrlUsers) {
        draft.unlinkInput(camel::runtime::DraftEdgeKind::Ctrl, user, id);
    }
    for (gc_node_ref_t user : users) {
        if (std::ranges::find(syncUsers, user) != syncUsers.end()) {
            // Its place in the chain orders it, and eraseNode writes the edge that says so.
            continue;
        }
        for (gc_node_ref_t pred : preds) {
            // A wait that came through the chain is handed on in that kind: the consumer that is a
            // statement of the block follows the same predecessor through a sync edge
            // (docs/handover/README.cn.md 5.21 item 4).
            if (std::ranges::find(chainWait, pred) != chainWait.end() &&
                handOverSyncWait(draft, pred, user)) {
                continue;
            }
            camel::runtime::appendControlDependency(draft, pred, user);
        }
    }
    draft.eraseNode(id);
    // Closing the chain over a node hands its successor the place that node stood in. A node the
    // program did not write as a statement of the block stands in none -- a pure node the block's
    // value reads -- and one whose place is in another block keeps none of it either (the chain of
    // a block joins its own statements, docs/design/effects/edges.cn.md 4.6). The successor is then
    // left waiting on nothing, and everything the removed node waited on stops being reached from
    // the program's result, which is what ran those effects (section 2). It keeps that wait instead:
    // it runs where the removed code did, which is where its own statement was written.
    for (gc_node_ref_t user : syncUsers) {
        if (user == id || !draft.alive(user) || !draft.syncInputsOf(user).empty() ||
            !draft.ctrlInputsOf(user).empty()) {
            continue;
        }
        for (gc_node_ref_t pred : inheritedWait) {
            if (pred != user && draft.alive(pred)) {
                camel::runtime::appendControlDependency(draft, pred, user);
            }
        }
    }
}

namespace {

/// A hash of what a draft decoded from `graph` holds: the node records and their columns, the
/// payloads, the ports, the constants and the types. The graphs it refers to are not part of it.
uint64_t contentHash(const GCGraph &graph) {
    const auto *native = graph.nodePayload();
    if (native == nullptr || native->empty()) {
        return 0;
    }
    uint64_t hash = 0x243F6A8885A308D3ull;
    const auto mix = [&](uint64_t word) {
        hash = (hash ^ word) * 0x9E3779B97F4A7C15ull;
        hash ^= hash >> 29;
    };
    const auto bytes = [&](const void *data, size_t size) {
        mix(size);
        if (data == nullptr) {
            return;
        }
        const auto *at = static_cast<const unsigned char *>(data);
        for (; size >= 8; size -= 8, at += 8) {
            uint64_t word;
            std::memcpy(&word, at, 8);
            mix(word);
        }
        uint64_t tail = 0;
        std::memcpy(&tail, at, size);
        mix(tail);
    };
    for (runtime::gc_cnt_t c = 0; c < native->chunkCount; ++c) {
        const runtime::GCNodeChunk &chunk = *native->chunks[c];
        const size_t nodes       = chunk.nodeCount;
        // A node record's own typeId is hashed as its canonical id, field for field around it, so
        // that every other byte of the record is hashed exactly as it is stored.
        for (size_t i = 0; i < nodes; ++i) {
            const runtime::GCNode &record = chunk.nodes()[i];
            const auto *at = reinterpret_cast<const unsigned char *>(&record);
            bytes(at, offsetof(runtime::GCNode, typeId));
            mix(static_cast<uint64_t>(
                camel::core::type::TypeTable::instance().canonicalIdOf(record.typeId)));
            bytes(
                at + offsetof(runtime::GCNode, typeId) + sizeof(record.typeId),
                sizeof(runtime::GCNode) - offsetof(runtime::GCNode, typeId) -
                    sizeof(record.typeId));
        }
        bytes(chunk.origins, nodes * sizeof(*chunk.origins));
        bytes(chunk.permissions, chunk.permissions ? nodes * sizeof(*chunk.permissions) : 0);
        bytes(chunk.attrs, chunk.attrs ? nodes * sizeof(*chunk.attrs) : 0);
        bytes(chunk.regions, chunk.regions ? nodes * sizeof(*chunk.regions) : 0);
        bytes(chunk.edges, chunk.edgeCount * sizeof(*chunk.edges));
        bytes(chunk.payload, chunk.payloadWordCount * sizeof(*chunk.payload));
    }
    bytes(native->regionTable, native->regionCount * sizeof(*native->regionTable));
    bytes(native->regionPorts, native->regionPortCount * sizeof(*native->regionPorts));
    bytes(native->writings, native->writingCount * sizeof(*native->writings));
    bytes(native->normPorts, native->normPortCount * sizeof(*native->normPorts));
    bytes(native->withPorts, native->withPortCount * sizeof(*native->withPorts));
    bytes(native->closure, native->closureCount * sizeof(*native->closure));
    for (uint64_t scalar :
         {uint64_t{native->entryNode},
          uint64_t{native->exitNode},
          uint64_t{native->outputNode},
          uint64_t{native->returnNode},
          static_cast<uint64_t>(native->returnKind),
          // A type enters the hash as its canonical id (the id of the same structure with the
          // names in its key erased), so two graphs that differ only in which object a type is,
          // or in the dimension names one of them was written with, are one content.
          static_cast<uint64_t>(
              camel::core::type::TypeTable::instance().canonicalId(graph.funcType())),
          static_cast<uint64_t>(
              camel::core::type::TypeTable::instance().canonicalId(graph.runtimeDataType())),
          static_cast<uint64_t>(
              camel::core::type::TypeTable::instance().canonicalId(graph.staticDataType())),
          static_cast<uint64_t>(
              camel::core::type::TypeTable::instance().canonicalId(graph.closureType()))}) {
        mix(scalar);
    }
    const auto statics = graph.staticSlots();
    bytes(statics.data(), statics.size_bytes());
    return hash;
}

} // namespace

bool RewriteMemo::leftAlone(std::string_view passName, const GCGraph *graph) const {
    const auto it = contentsByPass_.find(std::string(passName));
    const uint64_t hash = contentHash(*graph);
    return hash != 0 && it != contentsByPass_.end() && it->second.contains(hash);
}

void RewriteMemo::remember(std::string_view passName, const GCGraph *graph) {
    if (const uint64_t hash = contentHash(*graph)) {
        contentsByPass_[std::string(passName)].insert(hash);
    }
}

const camel::runtime::Program *rewriteReachableGraphs(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    std::string_view passName, const std::function<size_t(GraphDraft &)> &rewrite,
    size_t *rewrites, RewriteMemo *memo, bool (*mayRewrite)(const GCGraph &),
    bool checkScreens) {
    camel::runtime::RuntimeGraphDraftSession session(context, program);
    size_t total = 0;
    for (GCGraph *g : session.collectReachableRuntimeGraphs()) {
        if ((mayRewrite && !mayRewrite(*g)) || (memo && memo->leftAlone(passName, g))) {
            if (checkScreens && rewrite(session.edit(g)) != 0) {
                throw std::runtime_error(
                    std::format(
                        "{} rewrote graph '{}', which its screen or memo had skipped.",
                        passName,
                        g->name()));
            }
            continue;
        }
        const size_t made = rewrite(session.edit(g));
        total += made;
        if (memo && made == 0) {
            memo->remember(passName, g);
        }
    }
    CAMEL_LOG_INFO_S("Opt", "{}: {} rewrite(s)", passName, total);
    if (rewrites) {
        *rewrites = total;
    }
    if (total == 0) {
        return program;
    }
    return session.commit();
}

} // namespace camel::passes::generic

