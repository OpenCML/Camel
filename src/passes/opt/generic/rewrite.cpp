// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Draft-editing helpers shared by the generic optimization passes (see rewrite.h).
 */

#include "rewrite.h"
#include "camel/runtime/control.h"

#include "camel/core/operator_traits.h"
#include "camel/runtime/draft_session.h"
#include "camel/utils/log.h"

#include <algorithm>
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

bool isPureOper(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return false;
    }
    const auto *body = reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data());
    return camel::core::OperatorTable::instance().isPure(body->oper);
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
        return true;
    case GCNodeKind::Oper:
        return isPureOper(draft, id);
    default:
        return false;
    }
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

namespace {

/// True for nodes that run in order with effects: calls, branches, effectful operators, and gates
/// or joins that wait on something.
bool carriesOrdering(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h) {
        return false;
    }
    switch (h->kind) {
    case camel::runtime::GCNodeKind::Func:
    case camel::runtime::GCNodeKind::Call:
    case camel::runtime::GCNodeKind::Join:
    case camel::runtime::GCNodeKind::Brch:
    case camel::runtime::GCNodeKind::Sync:
    case camel::runtime::GCNodeKind::Loop: // runs its graphs, which may have effects
        return true;
    case camel::runtime::GCNodeKind::Gate:
        return !draft.ctrlInputsOf(id).empty();
    case camel::runtime::GCNodeKind::Oper:
        return !isPureOper(draft, id);
    default:
        return false;
    }
}

} // namespace

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
        if (carriesOrdering(draft, n)) {
            sources.push_back(n);
            continue;
        }
        // A pure node on a control chain (a statement after an effect) runs after its control
        // predecessors as well.
        for (auto inputs : {draft.normInputsOf(n), draft.withInputsOf(n), draft.ctrlInputsOf(n)}) {
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
            camel::runtime::appendControlDependency(draft, pred, user);
        }
    }
    draft.eraseNode(id);
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
        bytes(chunk.nodes(), nodes * sizeof(runtime::GCNode));
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
          reinterpret_cast<uint64_t>(graph.funcType()),
          reinterpret_cast<uint64_t>(graph.runtimeDataType()),
          reinterpret_cast<uint64_t>(graph.staticDataType()),
          reinterpret_cast<uint64_t>(graph.closureType())}) {
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

