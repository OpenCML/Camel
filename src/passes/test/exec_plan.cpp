// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * One line per graph and node, in the order the back ends run them:
 *   PLAN <graph> <position> node <ref> <kind> region <id> [entry/tail of the arms]
 */

#include "camel/passes/test/exec_plan.h"

#include "camel/runtime/exec_plan.h"

const camel::runtime::Program *
ExecPlanTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    for (GCGraph *graph : program->graphs()) {
        if (!graph->hasNodePayload()) {
            continue;
        }
        const ExecPlan &plan = graph->execPlan();
        for (size_t i = 0; i < plan.topo.size(); ++i) {
            const gc_node_ref_t ref = plan.topo[i];
            const GCNode *node      = graph->node(ref);
            os << "PLAN " << graph->name() << " " << i << " node " << ref << " kind "
               << static_cast<int>(node->kind) << " region " << graph->nodeRegion(ref);
            if (node->kind == GCNodeKind::Brch) {
                const auto entries = plan.branchArmEntriesOf(i);
                const auto tails   = plan.branchArmTailsOf(i);
                for (size_t k = 0; k < entries.size(); ++k) {
                    os << " arm" << k << "=" << entries[k] << ".." << tails[k];
                }
            }
            os << "\n";
        }
    }
    return program;
}
