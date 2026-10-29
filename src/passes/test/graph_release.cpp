// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A graph a rewrite encoded and no program bound is released by the collector (a debt of the
 * graph store's release rules): a pass that specializes a call encodes a graph version on the way,
 * and a commit may replace it with a copy of its own before anything binds it. The version no
 * program holds must not outlive the pass in the graph space.
 *
 * The pass encodes a large graph into a session's work table (what a specialization is), shows
 * that a collection during the pass keeps it (the drafts still reach it), and then, with the
 * session gone and nothing bound, that the collection after the settle frees it. Prints
 * "GRAPH_RELEASE alive=<0|1> freed=<0|1> encoded=<bytes> before=<bytes> after=<bytes>".
 */

#include "camel/passes/test/graph_release.h"

#include "camel/passes/test/anchor.h"
#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/runtime/draft_session.h"

using camel::passes::test::anchorNode;
using camel::passes::test::blankNode;

const camel::runtime::Program *
GraphReleaseTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program->root() == nullptr) {
        return program;
    }
    const size_t before = camel::core::mm::graphSpace().usage().usedBytes;
    GCGraph *encoded   = nullptr;
    size_t afterEncode = before;
    bool alive         = false;
    {
        RuntimeGraphDraftSession session(context_, program);
        auto draft = GraphDraft::decode(*program, program->root());
        draft->setId(context_->graphIds().allocate());
        // A graph big enough to show in the space: a specialization of a graph that is inlined
        // is of the graph it specializes, which is not small either.
        for (size_t i = 0; i < 20000; ++i) {
            blankNode(*draft);
        }
        encoded    = draft->encode("test$unbound", "test$unbound");
        afterEncode = camel::core::mm::graphSpace().usage().usedBytes;
        session.adoptGraph(encoded);
        // A collection while the pass runs must keep it: the session's drafts reach it.
        context_->collectRuntimeGraphs();
        alive = camel::core::mm::graphSpace().isAllocated(encoded);
    }
    // The pass is over and nothing bound it: the collector may have it.
    context_->settleRuntimeGraphs(program);
    context_->collectRuntimeGraphs();
    const bool freed = !camel::core::mm::graphSpace().isAllocated(encoded);
    const size_t after = camel::core::mm::graphSpace().usage().usedBytes;
    os << "GRAPH_RELEASE alive=" << (alive ? 1 : 0) << " freed=" << (freed ? 1 : 0)
       << " encoded=" << (afterEncode - before) << " before=" << before << " after=" << after
       << "\n";
    return program;
}