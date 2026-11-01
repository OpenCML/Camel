// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * One line per graph and node, in the order the back ends run them:
 *   PLAN <graph> <position> node <ref> <kind> region <id> [entry/tail of the arms]
 *
 * Then one line per control region, which is the answer liveness is seeded from: a region's
 * `result` is a seed, its members are its range, and both are read here so that a change to
 * either is a reading and not an argument:
 *   REGION <graph> <id> kind <n> owner <ref> result <ref|-1> members <n> in-plan <n>
 */

#include "camel/passes/test/exec_plan.h"

#include <string>
#include <unordered_set>

#include "camel/runtime/control_regions.h"
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
        const ControlRegions regions(*graph);
        std::unordered_set<gc_node_ref_t> inPlan(plan.topo.begin(), plan.topo.end());
        for (uint32_t at = 1; at < static_cast<uint32_t>(regions.size()); ++at) {
            const uint32_t id      = regions.regionAt(at);
            const GCRegion *region = graph->region(id);
            if (region == nullptr) {
                continue;
            }
            size_t members = 0;
            size_t laid    = 0;
            for (gc_node_ref_t n = 0; n < static_cast<gc_node_ref_t>(graph->nodeCount()); ++n) {
                if (!graph->containsNodeRef(n) || regions.controlOf(n) != id) {
                    continue;
                }
                ++members;
                laid += inPlan.contains(n) ? 1 : 0;
            }
            os << "REGION " << graph->name() << " " << id << " kind "
               << static_cast<int>(region->kind) << " owner " << region->owner << " result "
               << (region->result == kInvalidNodeRef ? std::string("<none>")
                                                     : std::to_string(region->result))
               << " members " << members << " in-plan " << laid << "\n";
        }
    }
    return program;
}
