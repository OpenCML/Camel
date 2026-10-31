// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "fault.h"

#include "camel/passes/test/anchor.h"
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
