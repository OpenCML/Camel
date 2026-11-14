// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Draft-editing helpers shared by the generic optimization passes (see rewrite.h).
 */

#include "rewrite.h"
#include "camel/execute/pass/base.h"
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

std::string_view operUriOf(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft = transaction.graph();
    const auto *h     = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return {};
    }
    return reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data())->uri();
}

camel::core::OperId
operIdOf(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft = transaction.graph();
    const auto *h     = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return camel::core::kNoOper;
    }
    return reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data())->oper;
}

/// Whether the pass may treat a call of the operator as a value of its own: fold it, move it,
/// merge it with an identical call or drop it. An operator that must run where the program writes
/// it is not one, whatever its effect (a rule's value and an `analysis` are pure and unfoldable).
bool isPureOper(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft = transaction.graph();
    const auto *h     = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return false;
    }
    const auto *body = reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data());
    return camel::core::OperatorTable::instance().mayBeRecomputed(body->oper);
}

bool isValueOnly(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft = transaction.graph();
    const auto *h     = draft.header(id);
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

bool isDeadWhenUnread(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft = transaction.graph();
    if (isValueOnly(transaction, id)) {
        return true;
    }
    return camel::runtime::isUnanchoredPure(draft, id);
}

bool isReplaceable(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft = transaction.graph();
    // The entry node is only a marker (erasing it resets it, and encoding then derives it from
    // the first node), so it does not pin a node; inlining often leaves it on a dead value.
    return id != draft.exitNode() && id != draft.outputNode() && id != draft.returnNode();
}

bool reaches(
    const camel::runtime::GraphTransaction &transaction, gc_node_ref_t from, gc_node_ref_t to) {
    const auto &draft = transaction.graph();
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
        for (auto users :
             {draft.normUsersOf(n),
              draft.withUsersOf(n),
              draft.ctrlUsersOf(n),
              draft.syncUsersOf(n)}) {
            work.insert(work.end(), users.begin(), users.end());
        }
    }
    return false;
}

bool readsNoWrittenSlot(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft = transaction.graph();
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
             {draft.normInputsOf(at),
              draft.withInputsOf(at),
              draft.ctrlInputsOf(at),
              draft.syncInputsOf(at)}) {
            work.insert(work.end(), inputs.begin(), inputs.end());
        }
    }
    return true;
}

std::vector<gc_node_ref_t>
orderedSources(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft = transaction.graph();
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
             {draft.normInputsOf(n),
              draft.withInputsOf(n),
              draft.ctrlInputsOf(n),
              draft.syncInputsOf(n)}) {
            work.insert(work.end(), inputs.begin(), inputs.end());
        }
    }
    return sources;
}

/// Whether the node's running is something the program observes, apart from its value being read:
/// an operator with an effect or one that may fault (design principles 3.4's predicate, asked with
/// the shapes of the types so that an elementwise operator over fixed shapes is a computation that
/// cannot fail), a conversion that can fail, or a call -- the callee may do either.
///
/// An arm, a join, a gate, a loop or a function value is structure, and removing the last read of
/// one removes no running of its own: the read a branch hands on is its arms' running, not the
/// branch's.
///
/// It asks through the transaction, the way `orderedSources` does, and names no draft type: the
/// draft is on its way out (graph-store step 2 b's direct-connection ledger), and every question
/// here reaches it through `transaction.graph()` rather than by adding a new connection to it.
bool isObservedRun(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft  = transaction.graph();
    const auto *header = draft.header(id);
    if (header == nullptr) {
        return false;
    }
    switch (header->kind) {
    case GCNodeKind::Oper:
    case GCNodeKind::Cast:
        return !camel::runtime::nodeIsFreelySchedulable(draft, id, true);
    case GCNodeKind::Call:
    case GCNodeKind::Func:
        return true;
    default:
        return false;
    }
}

namespace {

/// The nodes `removed` reads whose last read removing it takes: the nodes that carry ordering
/// (an effect, a call, a computation that may fault) that its value is computed from, that nothing
/// else reads. Reads by a node `alsoGoing` names do not count: they are about to go away as well
/// (a dissolved branch and its join, the code of an arm that is not taken).
std::vector<gc_node_ref_t> lastReadsGoingOut(
    const camel::runtime::GraphTransaction &transaction, gc_node_ref_t removed,
    const std::function<bool(gc_node_ref_t)> &alsoGoing) {
    const auto &draft = transaction.graph();
    std::vector<gc_node_ref_t> lastRead;
    for (gc_node_ref_t source : orderedSources(transaction, removed)) {
        if (!draft.alive(source) || (alsoGoing && alsoGoing(source)) ||
            !isObservedRun(transaction, source)) {
            continue;
        }
        bool readElsewhere = false;
        for (auto group :
             {draft.normUsersOf(source),
              draft.withUsersOf(source),
              draft.ctrlUsersOf(source),
              draft.syncUsersOf(source)}) {
            for (gc_node_ref_t user : group) {
                if (user == source || user == removed) {
                    continue;
                }
                readElsewhere = readElsewhere || !alsoGoing || !alsoGoing(user);
            }
        }
        if (!readElsewhere) {
            lastRead.push_back(source);
        }
    }
    // Source order is creation order: the effects of one expression are made in the order they are
    // written, and they run in that order.
    std::sort(lastRead.begin(), lastRead.end());
    lastRead.erase(std::unique(lastRead.begin(), lastRead.end()), lastRead.end());
    return lastRead;
}

} // namespace

gc_node_ref_t keepThroughValue(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t removed, gc_node_ref_t value,
    const std::function<bool(gc_node_ref_t)> &alsoGoing) {
    auto &draft = transaction.edit();
    // A value that is invalid is the removal with nothing in its place: the node going away is read
    // by no one, so there is no reader to hand the node's own wait to, and the keeper reads that
    // node's value -- it is the statement that reads it and stands where the node stood.
    const bool carried = value != kInvalidNodeRef;
    if (carried && !draft.alive(value)) {
        return value;
    }
    const auto lastRead = lastReadsGoingOut(transaction, removed, alsoGoing);
    if (lastRead.empty()) {
        return value;
    }
    // A node the value itself is computed from keeps a read through it: the readers of the value
    // read that node on the way, so the removal takes no read of it.
    std::vector<gc_node_ref_t> goingOut;
    for (gc_node_ref_t source : lastRead) {
        if (!carried) {
            goingOut.push_back(source);
        } else if (source != value && !reaches(transaction, source, value)) {
            goingOut.push_back(source);
        }
    }
    if (goingOut.empty()) {
        return value;
    }
    gc_node_ref_t kept = value;
    // The node stands where the removed node stood: it is body of the same block, so it is written
    // into the same region and not into whichever one the draft last inserted into.
    const uint32_t region   = draft.nodeRegion(removed);
    const uint32_t insertAt = draft.insertRegion();
    if (region != insertAt) {
        draft.setInsertRegion(region);
    }
    // What stands in the value is a `GATE` (design principles 3.3, docs/design/effects/edges.cn.md
    // 5.3): one data port reading the value that was there, and one sync predecessor naming the
    // node whose running the removal would otherwise take away -- a sync edge carries "used", so
    // the effect still runs, while the other readers of the value are neither pushed behind a
    // chain nor pulled alive by one. No order edge (it only orders, F2) and no keeper operator of
    // its own: a second mechanism for the same thing is what 2.3 rules out.
    for (gc_node_ref_t source : goingOut) {
        const gc_node_ref_t passed = carried ? kept : source;
        const auto *header         = draft.header(passed);
        if (header == nullptr || header->dataType == nullptr) {
            break;
        }
        const gc_node_ref_t node = draft.addGateNode(header->dataType, header->dataIndex);
        if (node == kInvalidNodeRef) {
            break;
        }
        const gc_node_ref_t input[1] = {passed};
        draft.setNormInputs(node, input);
        if (!draft.setSyncPredecessor(node, source)) {
            // The node already stands in the chain of a `sync` block: its place keeps it running,
            // so there is no read to take away and nothing to keep. Leave the value as it was.
            draft.eraseNode(node);
            break;
        }
        // Diagnostics on the value the keeper passes on point where the effect was written: the
        // node the readers see is the one that took its place in the value.
        if (draft.nodeOrigin(node) == 0) {
            draft.setNodeOrigin(node, draft.nodeOrigin(source));
        }
        kept = node;
    }
    if (region != insertAt) {
        draft.setInsertRegion(insertAt);
    }
    return kept;
}

void replaceNode(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t id, gc_node_ref_t replacement) {
    auto &draft     = transaction.edit();
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
    for (gc_node_ref_t source : orderedSources(transaction, id)) {
        if (std::ranges::find(preds, source) == preds.end()) {
            preds.push_back(source);
        }
    }
    preds = camel::runtime::controlFrontier(transaction, preds);
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
    // let it stop running when the value it fed goes. That is the whole shape of `let r =
    // (println(5)
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
    for (gc_node_ref_t pred : orderedSources(transaction, id)) {
        addInheritedWait(pred);
    }

    if (replacement != kInvalidNodeRef) {
        // The value this node's readers take is the replacement, with every node whose last read
        // this removal takes hung on a `GATE` on the way (design principles 3.3): the readers' use
        // is what keeps an effect, or a computation that may fault, running, and an order edge is
        // not one (3.3 as amended, 9.7). A statement of a `sync` block is not this case: the
        // block's chain hands it a place below, and that path is left as it is.
        const gc_node_ref_t stood =
            syncUsers.empty() ? keepThroughValue(transaction, id, replacement) : replacement;
        draft.replaceAllValueUses(id, stood);
        // A value computed elsewhere (a constant, a forwarded input) now stands for this node's
        // source: diagnostics on it point where the replaced node came from.
        if (draft.nodeOrigin(stood) == 0) {
            draft.setNodeOrigin(stood, draft.nodeOrigin(id));
        }
        // A graph names the node its result comes from, and that name is a value use like any
        // other: when the removed node was the one named, the node that stands for its value takes
        // the name. `eraseNode` clears a name that points at a node it removes
        // (src/runtime/draft.cpp#eraseNode), so a caller that replaces the node the
        // program returns leaves the graph with no result at all -- which is what a graph that runs
        // but cannot be drawn looked like: `std::nvm` ran it, `std::jgir` named a node the document
        // did not hold, and `std::gir` stopped at "Runtime graph exit node is missing"
        // (src/passes/trans/dot/graphviz.cpp#dumpGraph).
        if (draft.exitNode() == id) {
            draft.setExitNode(stood);
        }
        if (draft.outputNode() == id) {
            draft.setOutputNode(stood);
        }
        if (draft.returnNode() == id) {
            draft.setReturnNode(stood, draft.returnKind());
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
                handOverSyncWait(transaction, pred, user)) {
                continue;
            }
            camel::runtime::appendControlDependency(transaction, pred, user);
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
        for (gc_node_ref_t source : orderedSources(transaction, id)) {
            if (!camel::runtime::carriesOrdering(draft, source, true)) {
                continue;
            }
            bool readElsewhere = false;
            for (auto group :
                 {draft.normUsersOf(source),
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
            draft.syncInputsOf(id).begin(),
            draft.syncInputsOf(id).end());
        const auto chainTo = [&](gc_node_ref_t user, gc_node_ref_t pred) {
            draft.setSyncInputs(user, {});
            return draft.appendChainStatement(user, pred);
        };
        for (size_t i = 1; i < lastRead.size(); ++i) {
            // An effect written inside another one's argument is computed after it; chaining it the
            // other way would close a cycle of inputs.
            if (isComputedAfter(transaction, lastRead[i - 1], lastRead[i]) ||
                isComputedAfter(transaction, lastRead[i], lastRead[i - 1])) {
                continue;
            }
            const gc_node_ref_t exit = chainTo(lastRead[i], lastRead[i - 1]);
            if (exit != kInvalidNodeRef) {
                lastRead[i - 1] = exit;
            }
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
    // the program's result, which is what ran those effects (section 2). It keeps that wait
    // instead: it runs where the removed code did, which is where its own statement was written.
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
            if (!handOverSyncWait(transaction, pred, user)) {
                camel::runtime::appendControlDependency(transaction, pred, user);
            }
        }
    }
}

size_t eraseOrphanedChainGates(camel::runtime::GraphTransaction &transaction) {
    return transaction.edit().eraseOrphanedChainGates();
}

namespace {

/// A hash of what a draft decoded from `graph` holds: the node records and their columns, the
/// payloads, the ports, the constants and the types. The graphs it refers to are not part of it.
uint64_t contentHash(const GCGraph &graph) {
    const auto *native = graph.nodePayload();
    if (native == nullptr || native->empty()) {
        return 0;
    }
    uint64_t hash  = 0x243F6A8885A308D3ull;
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
        const size_t nodes                = chunk.nodeCount;
        // A node record's own typeId is hashed as its canonical id, field for field around it, so
        // that every other byte of the record is hashed exactly as it is stored.
        for (size_t i = 0; i < nodes; ++i) {
            const runtime::GCNode &record = chunk.nodes()[i];
            const auto *at                = reinterpret_cast<const unsigned char *>(&record);
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
    const auto it       = contentsByPass_.find(std::string(passName));
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
    std::string_view passName,
    const std::function<size_t(camel::runtime::GraphTransaction &)> &rewrite, size_t *rewrites,
    RewriteMemo *memo, bool (*mayRewrite)(const GCGraph &), bool checkScreens) {
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
        // A rewrite's own edges are not the only ones it moves: a merge hands a node's waits to the
        // users of the node it removes, so the graph has to be judged as a whole once the rewrite
        // is written, not predicted from the node pair the rewrite names. Nothing here can undo a
        // merge that has been written -- so the session is dropped and the program keeps the graph
        // it had, which is a correct graph where the rewritten one was not proved to be. The
        // judgement is the freeze's own (camel/runtime/cycle.h: one list of edge kinds, one walk),
        // and the drop is counted so that a rule composing a cycle shows up in std::stats instead
        // of quietly doing less work.
        camel::runtime::DraftCycleEdges edges(session.edit(g).graph());
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
    const camel::runtime::Program *const committed = session.commit();
    // The check every pass ends with, at the granularity of one rewrite step: `std::opt` runs
    // fold, cse, algebra, dce and order through here, and until 2026-11-02 none of them was checked
    // -- the pass was, once, after all of them. A step that moves an effect past another on one
    // resource is refused here, so the next step never sees such a graph.
    if (context != nullptr &&
        checkPassBoundary(context, std::string(passName), *program, *committed)) {
        return nullptr;
    }
    return committed;
}

} // namespace camel::passes::generic
