// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "fault.h"

#include "../opt/generic/rewrite.h"
#include "camel/core/rtdata/func.h"
#include "camel/passes/test/anchor.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"

#include <utility>
#include <vector>

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
OrderOnlyEffectTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
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
            // Identify the effect before stripping its credentials.
            out << "order_only_effect: node=" << id << " uri=" << uri
                << " runtimeFlags=" << static_cast<int>(header->runtimeFlags) << "\n";
            // A node to hang the order edge on: the effect's own chain predecessor when it has one,
            // its first value input otherwise. The edge is what keeps the effect in the graph (a
            // node with no edge at all is not a graph).
            camel::runtime::gc_node_ref_t anchor = camel::runtime::kInvalidNodeRef;
            // The predecessor this effect waits for, asked of the chain that carries a wait rather
            // than of the control kind: a control link is written as a sync edge once P0-2 lands,
            // and then the branch below would be empty for every node (P0-2 report, fault sample).
            if (!draft.syncInputsOf(id).empty()) {
                anchor = draft.syncInputsOf(id).front();
            } else if (!draft.normInputsOf(id).empty()) {
                anchor = draft.normInputsOf(id).front();
            }
            if (anchor == camel::runtime::kInvalidNodeRef) {
                continue;
            }
            // Every edge the effect stands on goes, both directions; what is left is one order
            // edge -- an edge that orders two nodes that both run and makes neither of them run
            // (docs/design/effects/edges.cn.md 1.5). Nothing reads the effect, no chain holds it,
            // no result returns it: the commit must refuse it.
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
            // edges.cn.md 1.5); it only keeps the node from being dropped as dead before the
            // credential check sees it.
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

namespace {

/// The static function a call's callee slot names, or nullptr. The walk is the one
/// `tryResolveStaticValue` makes for the same question (src/runtime/draft_inline.cpp): through the
/// GATE a `let`'s value gets and the COPY/CAST a rewrite leaves between a value and its user, to
/// the DATA node that stands for a constant of the static area. Only a callee this resolves is one
/// `std::opt::devirtualize` can rewrite, which is what makes the retarget below a case about that
/// pass.
///
/// The draft is a template parameter rather than a named type: a signature that names the type is a
/// new direct connection to it, which the ledger of graph-store step 2 b counts
/// (test/plans/feat/runtime/graph_draft_ledger, baseline.json), and this pass needs a draft, not an
/// address of one.
template <typename Draft>
::Function *staticCalleeOf(const Draft &draft, camel::runtime::gc_node_ref_t ref) {
    using camel::runtime::GCNodeKind;
    using camel::runtime::gc_node_ref_t;
    using camel::runtime::kInvalidNodeRef;
    for (int hop = 0; hop < 8 && ref != kInvalidNodeRef; ++hop) {
        const auto *header = draft.header(ref);
        if (header == nullptr) {
            return nullptr;
        }
        if (header->kind == GCNodeKind::Gate) {
            const auto in = draft.normInputsOf(ref);
            if (in.empty()) {
                return nullptr;
            }
            ref = in.back();
            continue;
        }
        if (header->kind == GCNodeKind::Copy || header->kind == GCNodeKind::Cast) {
            const auto in = draft.normInputsOf(ref);
            if (in.empty()) {
                return nullptr;
            }
            ref = in.front();
            continue;
        }
        if (header->kind != GCNodeKind::Data || header->dataIndex >= 0) {
            return nullptr;
        }
        // Only a value whose type says it is a function is one (the same test devirtualize makes
        // before it reads the slot: the static area holds every kind of constant).
        if (header->dataType == nullptr ||
            header->dataType->code() != camel::core::type::TypeCode::Function) {
            return nullptr;
        }
        const auto slots = draft.staticSlots();
        const auto index = static_cast<size_t>(-static_cast<int64_t>(header->dataIndex));
        if (index == 0 || index >= slots.size()) {
            return nullptr;
        }
        ::Function *function = camel::core::rtdata::fromSlot<::Function *>(slots[index]);
        return function != nullptr && function->graph() != nullptr ? function : nullptr;
    }
    return nullptr;
}

/// Whether calling `candidate` where `call` stands would be a call site that does not fit its
/// callee: a different number of ordinary arguments (`arity`), or the same number with an argument
/// no port takes. The judgement of "fits" is the repository's (`camel::runtime::typesCompatible`,
/// the relation the seal uses along an edge), so a case built with this pass asks about the same
/// relation the compiler asks about. The draft is a template parameter for the reason given above.
template <typename Draft>
bool givesMismatchedCall(
    const Draft &draft, camel::runtime::gc_node_ref_t call, const ::Function *candidate,
    bool arity) {
    const camel::runtime::GCGraph *target = candidate->graph();
    if (target == nullptr) {
        return false;
    }
    const auto args  = draft.normInputsOf(call);
    const auto ports = target->normPorts();
    if (arity) {
        return ports.size() != args.size();
    }
    if (ports.size() != args.size() || ports.empty()) {
        return false;
    }
    bool any = false;
    for (size_t i = 0; i < ports.size(); ++i) {
        const camel::runtime::GCNode *port = target->node(ports[i]);
        const auto *arg                    = draft.header(args[i]);
        if (port == nullptr || arg == nullptr) {
            return false;
        }
        if (!camel::runtime::typesCompatible(port->dataType(), arg->dataType)) {
            any = true;
        }
    }
    return any;
}

/// Points the first call through a static function at another static function of the same graph
/// whose signature the call site does not fit, and commits. Nothing else is touched: the arguments,
/// the result edge and the type the call hands on stay as they were, so the graph is a call site
/// that contradicts the type of the function it names.
const camel::runtime::Program *retargetFirstCall(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    bool arity) {
    using namespace camel::runtime;
    RuntimeGraphDraftSession session(context, program);
    for (GCGraph *owner : session.collectReachableRuntimeGraphs()) {
        auto &transaction = session.edit(owner);
        auto &draft       = transaction.edit();
        // The static functions this graph holds, in node order: what a call of it may be pointed
        // at without adding anything to the graph.
        std::vector<std::pair<gc_node_ref_t, ::Function *>> functions;
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (::Function *function = staticCalleeOf(draft, id)) {
                functions.emplace_back(id, function);
            }
        }
        if (functions.size() < 2) {
            continue;
        }
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            const auto *header = draft.header(id);
            if (header == nullptr || header->kind != GCNodeKind::Call) {
                continue;
            }
            const auto withs = draft.withInputsOf(id);
            if (withs.empty() || staticCalleeOf(draft, withs.front()) == nullptr) {
                continue;
            }
            for (const auto &candidate : functions) {
                if (candidate.first == withs.front() ||
                    !givesMismatchedCall(draft, id, candidate.second, arity)) {
                    continue;
                }
                // The callee slot is the first `with` input (a CALL's inputs are the callee, then
                // its `with` arguments; a FUNC is the same list without the callee).
                draft.replaceInput(
                    DraftEdgeKind::Data, id, withs.front(), candidate.first, PortRole::Context);
                return session.commit();
            }
        }
    }
    return program;
}

} // namespace

const camel::runtime::Program *
RecalleeArityTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return retargetFirstCall(context_, program, /*arity=*/true);
}

const camel::runtime::Program *
RecalleeTypeTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return retargetFirstCall(context_, program, /*arity=*/false);
}
