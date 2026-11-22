// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The passes of loop_range.h.
 */

#include "camel/passes/test/loop_range.h"

#include "camel/execute/graph_runtime_support.h"
#include "camel/execute/pass/registration.h"
#include "camel/runtime/exec_plan.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace {

using namespace camel::runtime;

/// Every node the visit of `ref` lays out before it: `ref` and the predecessors it recurses into
/// (executionPredecessorsOf), which is the block one `visit` of a wait emits at once.
std::unordered_set<gc_node_ref_t> laidOutWith(const GCGraph &graph, gc_node_ref_t ref) {
    std::unordered_set<gc_node_ref_t> block;
    std::vector<gc_node_ref_t> pending{ref};
    while (!pending.empty()) {
        const gc_node_ref_t at = pending.back();
        pending.pop_back();
        if (!block.insert(at).second) {
            continue;
        }
        for (const auto &predecessor : camel::execute::executionPredecessorsOf(graph, at)) {
            pending.push_back(predecessor.node);
        }
    }
    return block;
}

/// The plan of `graph`, written through as the order a back end runs, with its positions renumbered:
/// a test pass that reorders `topo` has to keep the position of a ref in step with it.
bool takePlan(GCGraph *graph, ExecPlan *&plan) {
    if (graph == nullptr || !graph->hasNodePayload() || graph->nodeCount() == 0) {
        return false;
    }
    plan = const_cast<ExecPlan *>(&graph->execPlan());
    return plan->topo.size() > 1;
}

void renumber(ExecPlan &plan) {
    for (uint32_t position = 0; position < plan.topo.size(); ++position) {
        plan.topoIndexByRef[plan.topo[position]] = position;
    }
}

/// Moves `topo[first, last)` to stand at `at` (positions after it keep their order).
void moveRange(ExecPlan &plan, uint32_t first, uint32_t last, uint32_t at) {
    const std::vector<gc_node_ref_t> moved(plan.topo.begin() + first, plan.topo.begin() + last);
    plan.topo.erase(plan.topo.begin() + first, plan.topo.begin() + last);
    const uint32_t to = at > first ? at - static_cast<uint32_t>(moved.size()) : at;
    plan.topo.insert(plan.topo.begin() + to, moved.begin(), moved.end());
    renumber(plan);
}

} // namespace

const camel::runtime::Program *
BreakLoopRangeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    for (GCGraph *graph : program->graphs()) {
        ExecPlan *plan = nullptr;
        if (!takePlan(graph, plan)) {
            continue;
        }
        for (const ExecLoop &loop : plan->loops) {
            const uint32_t li = loop.position;
            if (li == 0 || li >= plan->topo.size()) {
                continue;
            }
            // The wait the loop itself carries: an order edge into the LOOP node, which the layout
            // now visits before the loop's first round (computeExecutionOrder). 210b42cc9 fixed
            // that; putting the wait back after the loop's piece is the order that came out
            // of it before that fix, and it is the order this pass asks the tables about.
            const gc_node_ref_t loopRef = plan->topo[li];
            const auto orderPreds       = graph->orderPredsOf(loopRef);
            if (orderPreds.empty()) {
                continue;
            }
            const gc_node_ref_t wait = orderPreds.front().pred;
            if (wait >= plan->topoIndexByRef.size() ||
                plan->topoIndexByRef[wait] == ExecPlan::kNotInPlan) {
                continue;
            }
            const uint32_t from = plan->topoIndexByRef[wait];
            if (from >= li - 1) {
                continue;
            }
            // The block one visit of the wait laid out: the wait and the nodes it recursed into,
            // which sat contiguously before it. It moves as it stood, after the loop's piece.
            const auto block = laidOutWith(*graph, wait);
            uint32_t first   = from;
            while (first > 0 && block.contains(plan->topo[first - 1])) {
                --first;
            }
            const uint32_t at = li - static_cast<uint32_t>(from + 1 - first);
            os << "test::break_loop_range: graph '" << graph->name() << "': the wait " << wait
               << " and the " << (from - first) << " nodes it is laid out with stand at " << at
               << " again, after the loop's piece and before the LOOP node " << loopRef << " at "
               << li << '\n';
            moveRange(*plan, first, from + 1, li);
            // The tables of that order, which is what a back end would run: this refuses a loop
            // whose nodes are not one contiguous range of it.
            rebuildLoopTablesForTests(*graph, *plan);
            return program;
        }
    }
    os << "test::break_loop_range: no loop carrying a wait was found\n";
    return program;
}

const camel::runtime::Program *
InterleaveLoopRangeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    for (GCGraph *graph : program->graphs()) {
        ExecPlan *plan = nullptr;
        if (!takePlan(graph, plan) || plan->loops.size() < 2) {
            continue;
        }
        const ExecLoop &first  = plan->loops.front();
        const ExecLoop &second = plan->loops[1];
        if (first.position < 2 || second.position <= first.position) {
            continue;
        }
        // The last node of the second loop's range, put in the middle of the first loop's range:
        // the two loops' nodes now interleave, and neither set is one range any more.
        const uint32_t member = second.position - 1;
        const uint32_t at     = (first.begin + first.position) / 2;
        if (at <= first.begin || at >= first.position) {
            continue;
        }
        os << "test::interleave_loop_range: graph '" << graph->name() << "': node "
           << plan->topo[member] << " of the loop of the LOOP node " << plan->topo[second.position]
           << " stands inside the range of the loop of the LOOP node " << plan->topo[first.position]
           << " at " << at << '\n';
        moveRange(*plan, member, member + 1, at);
        rebuildLoopTablesForTests(*graph, *plan);
        return program;
    }
    os << "test::interleave_loop_range: no graph with two loops was found\n";
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header
/// declares, so the name a pipeline writes and the class that answers it are one fact.
const PassKernel breakLoopRangeKernel(kBreakLoopRangeTestPass, makePass<BreakLoopRangeTestPass>());
const PassKernel interleaveLoopRangeKernel(kInterleaveLoopRangeTestPass, makePass<InterleaveLoopRangeTestPass>());

} // namespace
