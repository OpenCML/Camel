// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "fault.h"

#include "camel/passes/test/anchor.h"
#include "../opt/generic/rewrite.h"
#include "camel/runtime/draft_session.h"

using camel::passes::test::anchorNode;
using camel::passes::test::blankNode;

const camel::runtime::Program *
DropCallArgumentTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    using camel::runtime::GCNodeKind;
    camel::runtime::RuntimeGraphDraftSession session(context_, program);
    for (camel::runtime::GCGraph *owner : session.collectReachableRuntimeGraphs()) {
        auto &transaction = session.edit(owner);
        auto &draft       = transaction.edit();
        for (camel::runtime::gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            const auto *header = draft.header(id);
            if (!header || header->kind != GCNodeKind::Func || draft.normInputsOf(id).empty()) {
                continue;
            }
            // The FUNC now passes one argument fewer than its callee has parameters.
            draft.unlinkInput(
                camel::runtime::DraftEdgeKind::Data,
                id,
                draft.normInputsOf(id).back());
            return session.commit();
        }
    }
    return program;
}

namespace {

/// Runs `breakBranch(draft, brch, join)` on the first branch of `main` and commits.
template <typename Fn>
const camel::runtime::Program *breakFirstBranchOfMain(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    Fn breakBranch) {
    using camel::runtime::GCNodeKind;
    camel::runtime::RuntimeGraphDraftSession session(context, program);
    for (camel::runtime::GCGraph *owner : session.collectReachableRuntimeGraphs()) {
        if (owner->name() != "main") {
            continue;
        }
        auto &transaction = session.edit(owner);
        auto &draft       = transaction.edit();
        for (camel::runtime::gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            const auto *header = draft.header(id);
            if (!header || header->kind != GCNodeKind::Brch) {
                continue;
            }
            const auto join = reinterpret_cast<const camel::runtime::DraftBrchPayload *>(
                                  draft.payloadOf(id).data())
                                  ->join;
            breakBranch(transaction, id, join);
            return session.commit();
        }
    }
    return program;
}

} // namespace

const camel::runtime::Program *
LeakBranchArmTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return breakFirstBranchOfMain(
        context_,
        program,
        [](camel::runtime::GraphTransaction &transaction,
           camel::runtime::gc_node_ref_t brch,
           camel::runtime::gc_node_ref_t join) {
            auto &draft = transaction.edit();
            // A node that runs because of the BRCH and that no arm's result depends on.
            const auto stray = anchorNode(transaction);
            draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, stray, brch);
            draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, join, stray);
        });
}

const camel::runtime::Program *
ShareBranchArmTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return breakFirstBranchOfMain(
        context_,
        program,
        [](camel::runtime::GraphTransaction &transaction,
           camel::runtime::gc_node_ref_t,
           camel::runtime::gc_node_ref_t join) {
            auto &draft = transaction.edit();
            // The second arm's result now depends on the first arm's.
            const auto tails = draft.withInputsOf(join);
            if (tails.size() >= 2 && tails[0] != tails[1]) {
                const auto first = tails[0], second = tails[1];
                draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, second, first);
            }
        });
}

const camel::runtime::Program *
OrderOnlyEffectTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    using camel::runtime::DraftEdgeKind;
    using camel::runtime::GCNodeKind;
    camel::runtime::RuntimeGraphDraftSession session(context_, program);
    for (camel::runtime::GCGraph *owner : session.collectReachableRuntimeGraphs()) {
        auto &transaction = session.edit(owner);
        auto &draft       = transaction.edit();
        for (camel::runtime::gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            const auto *header = draft.header(id);
            if (header == nullptr || header->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto uri = camel::passes::generic::operUriOf(transaction, id);
            // Not an effect: the strip is about what an effect stands on. `:io/` is the family of
            // operators whose running is observed (a print, a save); the others compute values.
            if (!uri.starts_with(":io/")) {
                continue;
            }
            // Every edge the effect stands on goes, both directions, and no edge is put back: what
            // is left is the node and the `kGCNodeFlagSyncStatement` mark it carries. The mark says
            // someone believed the node is in a chain; with no sync edge under it, it holds nothing
            // (design principles 3.1, and the mark was struck as a credential on 2026-11-01). So the
            // commit must refuse this graph, and it must refuse it on the credential: "neither a read
            // of its value nor a place in its chain".
            for (camel::runtime::gc_node_ref_t pred : draft.normInputsOf(id)) {
                draft.unlinkInput(DraftEdgeKind::Data, id, pred);
            }
            for (camel::runtime::gc_node_ref_t pred : draft.withInputsOf(id)) {
                draft.unlinkInput(DraftEdgeKind::Data, id, pred, camel::runtime::PortRole::Context);
            }
            for (camel::runtime::gc_node_ref_t pred : draft.ctrlInputsOf(id)) {
                draft.unlinkInput(DraftEdgeKind::Ctrl, id, pred);
            }
            for (camel::runtime::gc_node_ref_t pred : draft.syncInputsOf(id)) {
                draft.unlinkInput(DraftEdgeKind::Sync, id, pred);
            }
            // Both directions: an effect another node reads, or a chain that holds it as its
            // predecessor, still stands on that edge, so those edges go too.
            for (camel::runtime::gc_node_ref_t user : draft.normUsersOf(id)) {
                draft.unlinkInput(DraftEdgeKind::Data, user, id);
            }
            for (camel::runtime::gc_node_ref_t user : draft.withUsersOf(id)) {
                draft.unlinkInput(DraftEdgeKind::Data, user, id, camel::runtime::PortRole::Context);
            }
            for (camel::runtime::gc_node_ref_t user : draft.ctrlUsersOf(id)) {
                draft.unlinkInput(DraftEdgeKind::Ctrl, user, id);
            }
            for (camel::runtime::gc_node_ref_t user : draft.syncUsersOf(id)) {
                draft.unlinkInput(DraftEdgeKind::Sync, user, id);
            }
            // The effect keeps one edge and only one: an order edge out of it, to a node of its own
            // region (an order edge that enters a region breaks edges.cn.md 4.6 first, and that
            // refusal is not the one this instrument is for). Order is not a credential (3.1,
            // edges.cn.md 1.5; the author struck it on 2026-11-01), so what is left of the effect is
            // the mark alone, and the order edge is what keeps the node from being dropped as dead
            // before the credential check sees it.
            camel::runtime::gc_node_ref_t other = camel::runtime::kInvalidNodeRef;
            for (camel::runtime::gc_node_ref_t candidate = 0; candidate < draft.nodeSlotCount();
                 ++candidate) {
                if (candidate == id || draft.header(candidate) == nullptr ||
                    draft.nodeRegion(candidate) != draft.nodeRegion(id)) {
                    continue;
                }
                other = candidate;
                break;
            }
            if (other != camel::runtime::kInvalidNodeRef && draft.orderInputsOf(other).empty()) {
                const camel::runtime::gc_node_ref_t orders[1] = {id};
                draft.setOrderInputs(other, orders);
            }
            return session.commit();
        }
    }
    return program;
}
