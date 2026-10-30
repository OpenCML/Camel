// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Draft-editing helpers shared by the generic optimization passes (see rewrite.h).
 */

#include "rewrite.h"
#include "camel/runtime/control.h"
#include "camel/runtime/cycle.h"

#include "camel/core/operator_traits.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_writes.h"
#include "camel/utils/log.h"

#include "camel/core/type/intern.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <span>
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
        for (auto users : {draft.normUsersOf(n),
                           draft.withUsersOf(n),
                           draft.ctrlUsersOf(n),
                           draft.syncUsersOf(n)}) {
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
    //
    // The statements the removed node's value was computed from belong to that wait too. Reading a
    // value runs its producer (docs/design/effects/edges.cn.md 2), so an effect that the removed
    // node read ran exactly where the removed node did; a successor that keeps only the waits would
    // let it stop running when the value it fed goes. That is the whole shape of `let r = (println(5)
    // + println(6)) - 1` with `r` unread: the sum is the block's value, the block's GATE waits on
    // it, and the prints are reached only through the sum. The GATE is the end of the chain, not an
    // unused node, and dropping the sum must leave it waiting on the prints themselves.
    std::vector<gc_node_ref_t> inheritedWait;
    const auto addInheritedWait = [&](gc_node_ref_t pred) {
        const camel::runtime::DraftNodeHeader *header = draft.header(pred);
        if (header != nullptr && header->kind == GCNodeKind::Brch) {
            return;
        }
        if (std::ranges::find(inheritedWait, pred) == inheritedWait.end()) {
            inheritedWait.push_back(pred);
        }
    };
    for (auto waits : {draft.ctrlInputsOf(id), draft.syncInputsOf(id)}) {
        for (gc_node_ref_t pred : waits) {
            addInheritedWait(pred);
        }
    }
    for (gc_node_ref_t pred : orderedSources(draft, id)) {
        addInheritedWait(pred);
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
    // An effect this node was the last reader of keeps running where the node stood (design
    // principles 3.4, 9.7): the statements of a `sync` block happen where they are written, and the
    // effect is inside one of them whatever layer of its expression it is at. `println(5) - 1`
    // written as a statement prints `5` whether or not anything reads the difference, so removing
    // the unread difference may not remove the print with it. The effect takes the statement's
    // place in the chain -- what follows the statement follows the effect (5.7's handover, the same
    // one a splice uses) -- rather than being left reached by nothing when the chain closes over
    // the node.
    if (!draft.syncUsersOf(id).empty()) {
        std::vector<gc_node_ref_t> lastRead;
        for (gc_node_ref_t source : orderedSources(draft, id)) {
            if (!camel::runtime::carriesOrdering(draft, source, true)) {
                continue;
            }
            bool readElsewhere = false;
            for (auto group : {draft.normUsersOf(source),
                               draft.withUsersOf(source),
                               draft.ctrlUsersOf(source),
                               draft.syncUsersOf(source)}) {
                for (gc_node_ref_t user : group) {
                    readElsewhere = readElsewhere || (user != id && user != source);
                }
            }
            if (!readElsewhere) {
                lastRead.push_back(source);
            }
        }
        // Source order is creation order: the effects of one expression are made in the order they
        // are written, and they run in that order.
        std::sort(lastRead.begin(), lastRead.end());
        lastRead.erase(std::unique(lastRead.begin(), lastRead.end()), lastRead.end());
        const std::vector<gc_node_ref_t> chainPreds(
            draft.syncInputsOf(id).begin(), draft.syncInputsOf(id).end());
        const auto chainTo = [&](gc_node_ref_t user, gc_node_ref_t pred) {
            markSyncStatement(draft, user);
            markSyncStatement(draft, pred);
            const gc_node_ref_t only[1] = {pred};
            draft.setSyncInputs(user, only);
            camel::runtime::appendControlDependency(draft, pred, user);
        };
        for (size_t i = 1; i < lastRead.size(); ++i) {
            // An effect written inside another one's argument is computed after it; chaining it the
            // other way would close a cycle of inputs.
            if (isComputedAfter(draft, lastRead[i - 1], lastRead[i]) ||
                isComputedAfter(draft, lastRead[i], lastRead[i - 1])) {
                continue;
            }
            chainTo(lastRead[i], lastRead[i - 1]);
        }
        if (!lastRead.empty()) {
            draft.handChainPlaceTo(id, lastRead.back());
            // An expression with several effects in it is a chain of them, and what the statement
            // was written after is what the first of them is written after: the place was handed to
            // the last one above.
            if (lastRead.size() > 1 && draft.alive(lastRead.front()) &&
                draft.syncInputsOf(lastRead.front()).empty()) {
                for (gc_node_ref_t pred : chainPreds) {
                    if (pred != lastRead.front() && draft.alive(pred)) {
                        chainTo(lastRead.front(), pred);
                        break;
                    }
                }
            }
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
            if (pred == user || !draft.alive(pred)) {
                continue;
            }
            // The chain takes the tail as a wait of its own kind when it can (`handOverSyncWait`),
            // which is what the block's value does: it waits on the statement written before it
            // (docs/design/effects/edges.cn.md 4.6, 5.3 item 3). Otherwise the wait is control.
            if (!handOverSyncWait(draft, pred, user)) {
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

namespace {

/// Reads a draft for `camel::runtime::findInputCycle`: the draft keeps all five edge kinds as runs
/// of refs, so the adaptor is only a switch over the kind. Template over the draft type so that it
/// names no type it does not have to: the ledger of direct connections to the editable graph
/// (test/cases/runtime/graph_draft_ledger) may only fall.
template <class Draft>
class DraftCycleEdges {
  public:
    explicit DraftCycleEdges(const Draft &draft) : draft_(draft) {}

    size_t slotCount() const { return draft_.nodeSlotCount(); }
    bool present(gc_node_ref_t id) const { return draft_.alive(id); }
    std::span<const gc_node_ref_t>
    inputsOf(gc_node_ref_t id, camel::runtime::CycleEdgeKind kind) const {
        switch (kind) {
        case camel::runtime::CycleEdgeKind::Norm:
            return draft_.normInputsOf(id);
        case camel::runtime::CycleEdgeKind::With:
            return draft_.withInputsOf(id);
        case camel::runtime::CycleEdgeKind::Ctrl:
            return draft_.ctrlInputsOf(id);
        case camel::runtime::CycleEdgeKind::Sync:
            return draft_.syncInputsOf(id);
        case camel::runtime::CycleEdgeKind::Order:
            return draft_.orderInputsOf(id);
        }
        return {};
    }

  private:
    const Draft &draft_;
};

} // namespace
const camel::runtime::Program *rewriteReachableGraphs(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    std::string_view passName, const std::function<size_t(GraphDraft &)> &rewrite,
    size_t *rewrites, RewriteMemo *memo, bool (*mayRewrite)(const GCGraph &),
    bool checkScreens) {
    camel::runtime::RuntimeGraphDraftSession session(context, program);
    size_t total = 0;
    for (GCGraph *g : session.collectReachableRuntimeGraphs()) {
        if ((mayRewrite && !mayRewrite(*g)) || (memo && memo->leftAlone(passName, g))) {
            if (checkScreens && rewrite(session.edit(g).edit()) != 0) {
                throw std::runtime_error(
                    std::format(
                        "{} rewrote graph '{}', which its screen or memo had skipped.",
                        passName,
                        g->name()));
            }
            continue;
        }
        const size_t made = rewrite(session.edit(g).edit());
        // A rewrite's own edges are not the only ones it moves: a merge hands a node's waits to the
        // users of the node it removes, so the graph has to be judged as a whole once the rewrite is
        // written, not predicted from the node pair the rewrite names. Nothing here can undo a merge
        // that has been written -- so the session is dropped and the program keeps the graph it had,
        // which is a correct graph where the rewritten one was not proved to be. The judgement is
        // the freeze's own (camel/runtime/cycle.h: one list of edge kinds, one walk), and the drop is
        // counted so that a rule composing a cycle shows up in std::stats instead of quietly doing
        // less work.
        DraftCycleEdges edges(session.edit(g).graph());
        if (camel::runtime::findInputCycle(edges) != camel::runtime::kInvalidNodeRef) {
            camel::runtime::recordCycleDroppedSession();
            return program;
        }
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

