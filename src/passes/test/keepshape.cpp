// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The passes of keepshape.h: one removes the node that takes a print's last read in the one graph
 * shape where the print has no `sync` edge to stand on, and says what the rewrite did with it; the
 * other counts the `:op/keep` nodes a pass list has produced, without changing the program.
 *
 * A test pass cannot make this shape by writing a print of its own: a pass that gives a program an
 * effect it did not have is refused at the pass boundary --
 * `RuntimeDiag::PassBoundaryResources: Pass 'test::keepshape' changed the ambient resources the
 * program touches: before nothing, after stdout` -- so the shape has to be built around a print the
 * source wrote. That is the wall the next test-only pass of this kind hits first, and the reason
 * `keep_shape.cml` writes its print in a function that is no `sync` block.
 */

#include "camel/passes/test/keepshape.h"

#include "../opt/generic/rewrite.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/effect_liveness.h"
#include "camel/runtime/graph_txn.h"

#include <string_view>

namespace {

using namespace camel::runtime;

constexpr std::string_view kPrintUri = ":io/println";

/// The node whose removal takes the print's last read: a node that reads a print's value, in a
/// graph where nothing has a sync edge (the source writes the print in a function that is no
/// `sync` block).
gc_node_ref_t readerOfPrint(GraphTransaction &transaction, const GCGraph *graph) {
    auto &draft = transaction.edit();
    for (gc_node_ref_t id = 0; id < graph->nodeCount(); ++id) {
        if (graph->node(id) == nullptr) {
            continue;
        }
        for (gc_node_ref_t source : draft.normInputsOf(id)) {
            if (camel::passes::generic::operUriOf(transaction, source) == kPrintUri) {
                return id;
            }
        }
    }
    return kInvalidNodeRef;
}

gc_node_ref_t addInt(GraphTransaction &transaction, int64_t value) {
    return transaction.edit().materializeStaticValue(
        camel::core::rtdata::toSlot<int64_t>(value), camel::core::type::Type::Int64());
}

} // namespace

const camel::runtime::Program *
KeepShapeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    for (GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        auto &transaction      = session.edit(graph);
        const gc_node_ref_t id = readerOfPrint(transaction, graph);
        if (id == kInvalidNodeRef) {
            continue;
        }
        auto &draft = transaction.edit();
        // The print whose last read the removal takes.
        gc_node_ref_t print = kInvalidNodeRef;
        for (gc_node_ref_t source : draft.normInputsOf(id)) {
            if (camel::passes::generic::operUriOf(transaction, source) == kPrintUri) {
                print = source;
            }
        }
        const gc_node_ref_t ninetyNine = addInt(transaction, 99);
        camel::passes::generic::replaceNode(transaction, id, ninetyNine);

        // What stands in the value: `:op/keep` when the pass routed the print through it, nothing
        // when it did not. The keep node reads the print, which is exactly what a data use of the
        // print looks like, so its presence is asked of the print's value users.
        std::string_view uri{};
        for (gc_node_ref_t user : draft.normUsersOf(print)) {
            if (camel::passes::generic::operUriOf(transaction, user) == ":op/keep") {
                uri = ":op/keep";
            }
        }
        const bool keep = uri == ":op/keep";

        // Whether the print is kept by a value of it being read -- the question
        // `effectStandingOf` answers (camel/runtime/effect_liveness.h). A control edge is not a
        // data use: `waited_on` says whether an order edge keeps the print running anyway, which is
        // why running the program alone cannot tell the two apart.
        EffectLivenessFacts facts;
        facts.hasDataUser =
            !draft.normUsersOf(print).empty() || !draft.withUsersOf(print).empty();
        facts.isSyncStatement =
            !draft.syncInputsOf(print).empty() || !draft.syncUsersOf(print).empty();
        facts.hasWaitEdge = !draft.ctrlInputsOf(print).empty();
        os << "KEEPSHAPE keep=" << (keep ? 1 : 0) << " uri=" << (uri.empty() ? "-" : uri)
           << " data_user=" << (facts.hasDataUser ? 1 : 0)
           << " sync_statement=" << (facts.isSyncStatement ? 1 : 0)
           << " wait_edge=" << (facts.hasWaitEdge ? 1 : 0)
           << " waited_on=" << (draft.ctrlUsersOf(print).empty() ? 0 : 1)
           // **This column does not answer "would a fusion group take this node in".** It prints the
           // `with`-user term alone; the other term -- a `norm` user outside the group -- is decided by
           // a separate loop (`fusion_group`'s `isPrivate`), which returns before this expression is ever
           // asked. So `fusion_private=1` beside `data_user=1` is not a contradiction: the two columns
           // answer two questions, one about a credential and one about a structure. A reading shipped
           // here once as if they were one fact and was retired for that reason.
           // The expression below is the would-be question:
           // `fusion_group`'s `isPrivate` asks, but **reading the raw fact itself** (`withUsersOf`), never
           // `facts.hasDataUser`: that field is already this pass's answer (`norm` + `with`), so
           // borrowing it would prove this side's conclusion with this side's conclusion. Do not
           // simplify this back into `!facts.hasDataUser` -- that version cannot be 1 by construction.
           << " fusion_private="
           << ((draft.withUsersOf(print).empty() && draft.ctrlUsersOf(print).empty() &&
                !facts.isSyncStatement)
                   ? 1
                   : 0)
           << " standing="
           << (keepsEffectAlive(effectStandingOf(facts)) ? "alive" : "none") << "\n";
        return session.commit();
    }
    os << "KEEPSHAPE found=0\n";
    return program;
}
const camel::runtime::Program *
KeepInspectTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr) {
        return program;
    }
    // Read-only: the pass is the second half of a pass list, and what it counts is what the first
    // half wrote.
    size_t keeps   = 0;
    size_t inChain = 0;
    size_t noChain = 0;
    size_t waited  = 0;
    RuntimeGraphDraftSession session(context_, program);
    for (GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        auto &transaction = session.edit(graph);
        auto &draft       = transaction.edit();
        for (gc_node_ref_t id = 0; id < graph->nodeCount(); ++id) {
            if (graph->node(id) == nullptr ||
                camel::passes::generic::operUriOf(transaction, id) != ":op/keep") {
                continue;
            }
            ++keeps;
            // The node the operator keeps is its first input: the one no result takes, whose only
            // remaining credential is this read.
            const auto inputs = draft.normInputsOf(id);
            if (inputs.empty()) {
                ++noChain;
                continue;
            }
            const gc_node_ref_t kept = inputs.front();
            const bool place =
                !draft.syncInputsOf(kept).empty() || !draft.syncUsersOf(kept).empty();
            if (place) {
                ++inChain;
            } else {
                ++noChain;
            }
            // A wait is an order edge: a control edge either way, which is the credential the
            // operator exists to replace.
            if (!draft.ctrlInputsOf(kept).empty() || !draft.ctrlUsersOf(kept).empty()) {
                ++waited;
            }
        }
    }
    os << "KEEPSPOTS keeps=" << keeps << " in_chain=" << inChain << " out_of_chain=" << noChain
       << " waited=" << waited << "\n";
    return program;
}
