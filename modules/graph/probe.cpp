// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `graph::probe_handle`: the self-test of the check in Handle::create.
 *
 * A handle that names a program names a node that program has. Two kernels of this module once
 * claimed a node that existed only in a draft, and nothing said so until a reader looked the node
 * up in a graph that did not have it. The check refuses that claim where the handle is made; this
 * pass is how the refusal is exercised, once, in the gate -- with a handle that is wrong on
 * purpose, because a check nobody triggers is a comment.
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