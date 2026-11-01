// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pass of keepshape.h: it removes the node that takes a print's last read, in the one graph
 * shape where the print has no `sync` edge to stand on, and says what the rewrite did with it.
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
        facts.isWaitedOn  = !draft.ctrlUsersOf(print).empty();
        os << "KEEPSHAPE keep=" << (keep ? 1 : 0) << " uri=" << (uri.empty() ? "-" : uri)
           << " data_user=" << (facts.hasDataUser ? 1 : 0)
           << " sync_statement=" << (facts.isSyncStatement ? 1 : 0)
           << " wait_edge=" << (facts.hasWaitEdge ? 1 : 0)
           << " waited_on=" << (facts.isWaitedOn ? 1 : 0) << " standing="
           << (keepsEffectAlive(effectStandingOf(facts)) ? "alive" : "none") << "\n";
        return session.commit();
    }
    os << "KEEPSHAPE found=0\n";
    return program;
}