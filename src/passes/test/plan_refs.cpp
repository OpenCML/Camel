// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The plan's answer to "which references come back, and where" (camel/runtime/exec_plan.h, "The
 * references that come back"), printed so that it is a reading and not an intention. No back end
 * reads the plan's table yet, so nothing else in the tree can show it is right; this pass is what
 * makes a change to it visible.
 *
 * One line per graph, which is the balance:
 *   PLAN_REFS <graph> produced <n> copies <n> give-backs <n> kept <n> packed <n> tail <ref> ...
 *     ... tail-kind <n> balanced yes|no
 * then one line per producing position:
 *   PLAN_REFS <graph> at <position> value <ref> kind <n> copies <n> kept <0|1> uses <n>
 * and one line per reference that comes back: `give` when a use of the value ended, `packed` when the
 * value that packed it in died (a closure tuple or an aggregate), which is later than the use.
 *   PLAN_REFS <graph> at <position> give <ref> x<n>
 *   PLAN_REFS <graph> at <position> packed <ref> x<n>
 *
 * `produced` is the number of values the plan counts (ports, constants and nodes that only forward
 * a value are not among them), `copies` the references beyond each value's own, `give-backs` the
 * references that come back when a use ends, `packed` the ones that come back when the value that
 * packed them in dies (a closure tuple or an aggregate), `kept` the values the program itself keeps
 * (the graph's result). The columns are read in this order on purpose: the first five balance as
 * `produced + copies == give-backs + kept + packed`, and `balanced` is that arithmetic recomputed
 * here from the numbers the plan reports, not from the plan's own answer.
 *
 * A graph whose plan is empty prints only its summary line: a pass with nothing to say says it.
 */

#include "camel/passes/test/plan_refs.h"

#include <string>
#include <unordered_map>

#include "camel/runtime/exec_plan.h"

const camel::runtime::Program *
PlanRefsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    for (GCGraph *graph : program->graphs()) {
        if (!graph->hasNodePayload()) {
            continue;
        }
        const ExecPlan &plan = graph->execPlan();
        const std::string name = graph->name();
        const size_t produced  = plan.refProducedTotal();
        const size_t copies    = plan.refCopiesTotal();
        const size_t giveBacks = plan.refGiveBacksTotal();
        const size_t kept      = plan.refKeptTotal();
        const size_t packed    = plan.refPackedTotal();
        bool balanced          = plan.refBalanceHolds();
        // The same arithmetic, from the numbers printed: a reader must be able to check the line.
        balanced = balanced && produced + copies == giveBacks + kept + packed;
        os << "PLAN_REFS " << name << " produced " << produced << " copies " << copies
           << " give-backs " << giveBacks << " kept " << kept << " packed " << packed;
        if (plan.tailValueRef == kInvalidNodeRef) {
            os << " tail none";
        } else {
            const GCNode *tail = graph->node(plan.tailValueRef);
            os << " tail " << plan.tailValueRef
               << " tail-kind " << (tail == nullptr ? -1 : static_cast<int>(tail->kind));
        }
        os << " balanced " << (balanced ? "yes" : "no") << "\n";
        // The uses of each value, counted from the rows themselves: a reference comes back once per
        // use, and a value that nothing reads comes back at its own position instead.
        std::unordered_map<gc_node_ref_t, size_t> usesOf;
        for (size_t i = 0; i < plan.topo.size(); ++i) {
            for (const ExecPlan::RefGiveBack &row : plan.refGiveBacksAt(i)) {
                if (row.at != row.value) {
                    usesOf[row.value] += row.count;
                }
            }
        }
        for (size_t i = 0; i < plan.topo.size(); ++i) {
            const gc_node_ref_t ref = plan.topo[i];
            if (i < plan.refProducedAt.size() && plan.refProducedAt[i] != 0) {
                const GCNode *node = graph->node(ref);
                os << "PLAN_REFS " << name << " at " << i << " value " << ref << " kind "
                   << (node == nullptr ? -1 : static_cast<int>(node->kind)) << " copies "
                   << plan.refCopiesAt[i] << " kept " << static_cast<int>(plan.refKeptAt[i])
                   << " uses " << usesOf[ref] << "\n";
            }
            for (const ExecPlan::RefGiveBack &row : plan.refGiveBacksAt(i)) {
                os << "PLAN_REFS " << name << " at " << i << (row.packed ? " packed " : " give ")
                   << row.value << " x" << row.count << "\n";
            }
        }
    }
    return program;
}