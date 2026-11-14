// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The judgement `camel::runtime::deadWrite` makes, printed for a plan to pin
 * (`camel/runtime/effect_liveness.h`). Every graph of the program is walked, in node index order,
 * and every node whose declaration says it writes a place of the program prints one line:
 *
 *   DEADWRITE graph=<name> node=<ref> oper=<uri> rooted=<0|1> read=<0|1> dead=<0|1>
 *
 * The program is passed on unchanged: this is an inspection, not a rewrite.
 */

#include "camel/passes/test/deadwrite.h"

#include "camel/core/operator_table.h"
#include "camel/runtime/effect_liveness.h"

const camel::runtime::Program *
DeadWriteTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    const auto &table = camel::core::OperatorTable::instance();
    for (const GCGraph *graph : program->graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (uint32_t index = 0; index < graph->nodeCount(); ++index) {
            const gc_node_ref_t ref = static_cast<gc_node_ref_t>(index);
            const GCNode *node      = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto *body = graph->nodeBodyAs<GCOperBody>(ref);
            if (body == nullptr) {
                continue;
            }
            // A node that writes nothing is not what this pass reports: the judgement is about a
            // write, and printing every operation would bury the ones it is about.
            bool writes = table.hasExternalEffect(body->oper);
            const auto inputs = graph->normInputsOf(ref);
            for (size_t port = 0; port < inputs.size() && !writes; ++port) {
                writes = table.writesOperand(body->oper, false, port);
            }
            if (!writes) {
                continue;
            }
            os << "DEADWRITE graph=" << graph->name() << " node=" << ref
               << " oper=" << body->uri() << " rooted=" << (writeIsRooted(*graph, ref) ? 1 : 0)
               << " read=" << (valueIsRead(*graph, ref) ? 1 : 0)
               << " dead=" << (deadWrite(*graph, ref) ? 1 : 0) << "\n";
        }
    }
    return program;
}