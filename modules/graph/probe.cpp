// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `graph::probe_handle`: the self-test of two checks that answer "a node the program has".
 *
 * A handle that names a program names a node that program has. Two kernels of this module once
 * claimed a node that existed only in a draft, and nothing said so until a reader looked the node
 * up in a graph that did not have it. The check refuses that claim where the handle is made; this
 * pass is how the refusal is exercised, once, in the gate -- with a handle that is wrong on
 * purpose, because a check nobody triggers is a comment.
 *
 * The second check is `GCGraph::nodeRegion` (camel/runtime/graph.h). It used to answer 0 for a ref
 * that is not a node, and 0 is the body: the query handed back the membership "in the body" for a
 * node that is not there. It now fails instead. This pass asks it of the same wrong ref, after the
 * control that a node which is there still answers the body (0). The process ends by that abort,
 * so the plan asserts the abort and its reading rather than an exit code; a region query that went
 * back to answering 0 prints the line and exits 0, which the plan rejects.
 */

#include "probe.h"

#include "handle.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/execute/pass/base.h"
#include "camel/execute/pass/params.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <ostream>

namespace camel::graphmod {
namespace {

/// Makes one handle the program can name and one it cannot, and says what the construction did.
class HandleProbePass final : public GraphIRPass {
  public:
    explicit HandleProbePass(const camel::core::context::context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        const camel::runtime::GCGraph *graph =
            program != nullptr ? program->root() : nullptr;
        if (graph == nullptr) {
            os << "HANDLE_PROBE no root graph\n";
            return program;
        }
        // The control: node 0 of a graph that has it is a handle the program can name.
        Handle *named = Handle::create(
            HandleKind::Node, program, graph->id(), 0, nullptr, camel::core::mm::autoSpace());
        os << "HANDLE_PROBE allowed a node that is there: " << (named != nullptr ? "yes" : "no")
           << "\n";
        // The mistake: a node past the end of the graph, claimed as if it were published.
        const uint32_t missing = static_cast<uint32_t>(static_cast<uint32_t>(graph->nodeCount() + 7));
        try {
            Handle::create(
                HandleKind::Node, program, graph->id(), missing, nullptr,
                camel::core::mm::autoSpace());
            os << "HANDLE_PROBE allowed a node that is not there\n";
        } catch (const camel::core::error::RuntimeFault &fault) {
            os << "HANDLE_PROBE refused: " << fault.what() << "\n";
        }
        // The region query, on the same two refs. The control comes first: a node the graph has is
        // in the body (0) when no control region holds it, and that answer must not move.
        os << "REGION_PROBE body region of node 0: " << graph->nodeRegion(0) << std::flush << "\n";
        // The mistake: a node past the end of the graph has no region, and 0 would say "the body".
        // The guard in `nodeRegion` ends the process here, before the line below is printed.
        const uint32_t missingRegion = graph->nodeRegion(missing);
        os << "REGION_PROBE answered the body for a node that is not there: " << missingRegion
           << "\n";
        return program;
    }
};

} // namespace

void registerHandleProbePass() {
    // A test-only pass: reachable by name and in no pipeline, which is the whole switch.
    registerModulePass("graph::probe_handle", [](const camel::core::context::context_ptr_t &ctx) {
        return std::make_unique<HandleProbePass>(ctx);
    });
}

} // namespace camel::graphmod