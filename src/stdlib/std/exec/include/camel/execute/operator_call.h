// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The one place a node's rows are run, and the one place an operator is called.
 *
 * Four back ends call operators -- std::nvm, std::fvm, std::jit, and the taskflow scheduler -- and
 * the count's rows are applied here, once, rather than at each place a call can happen. A list that
 * has to be remembered at each call site is how a row goes missing: the SCHD instruction in
 * gotovm.cpp, fastvm.cpp's interpreter exit and the Oper branch of
 * TaskflowExecSchedPass::executePreparedNode each had their own, and a call that forgets it applies
 * no rows. Here the list is one call, so no back end has a place left to forget it and no back end
 * has a place left to add a second one.
 *
 * `runNode` is that same call for every other node kind: a value's rows are laid out at the
 * position of the node that runs when they are due, and only the operator kind reaches its position
 * through an operator call. A CALL, a FILL, a COPY and the rest make and read values too -- a
 * `state::Ref` made by a call is born with its count at the call -- so a back end that runs one of
 * those nodes runs its position's rows here, in the place it runs the node.
 *
 * What a back end keeps for itself stays inside the callable it passes: its fault wrapping, its
 * effect site, its profiler, its optimization, and the --gc-check anchor. Those are decisions about
 * the call. The count is not one: the plan decided it, and the callable has nothing to say about
 * it.
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/rtdata.h"
#include "camel/runtime/exec_plan.h"
#include "camel/utils/assert.h"

#include <memory>
#include <utility>

namespace camel::execute {

/// Where one operator call stands in the plan it runs in: `node` is the operator node the plan laid
/// the rows out for, and `frame` is the frame the call reads and writes.
///
/// `keepAlive` holds the plan for as long as the site exists, and a site exists across the call
/// itself. A plan is a fact of a graph version and the store that answers it collects its entries
/// while the program runs, so a reference taken before the call can outlive the entry it came from:
/// the call runs arbitrary code -- nested calls, a fault, the graph space reclaiming a version --
/// and the rows are applied after it returns. With the plan released in between, the rows read a
/// freed table (`applyRefEntriesForNode` then asks a frame for an index the dead plan no longer
/// describes). The shared reference is what keeps the answer to "which slot" alive for as long as
/// the answer is used; a site built from a plan whose owner is not at hand leaves it empty, and the
/// caller that knows the owner passes it.
struct OperatorCallSite {
    const camel::runtime::ExecPlan &plan;
    camel::core::context::Frame &frame;
    camel::runtime::gc_node_ref_t node;
    std::shared_ptr<const camel::runtime::ExecPlan> keepAlive;
};

/// The site of a bytecode call: bytecode holds the instruction's destination slot where the node
/// form holds the node, and the plan's own table is the translation (`refNodeOfSlot`) -- one a back
/// end must not write for itself, or two of them would disagree about which position an instruction
/// is. A slot no operator makes answers `kInvalidNodeRef`, which is a position with no rows.
inline OperatorCallSite
siteOfSlot(camel::core::context::Frame &frame, camel::runtime::gc_data_idx_t result) {
    const auto *graph = frame.graph();
    GUARD(graph != nullptr, "an operator ran in a frame that has no runtime graph");
    std::shared_ptr<const camel::runtime::ExecPlan> owner = camel::runtime::execPlanOf(*graph);
    return OperatorCallSite{*owner, frame, owner->refNodeOfSlot(result), owner};
}

/// The site of a call a back end runs as a node: the node itself, and the plan's owner held for the
/// call (the same reason as `siteOfSlot`).
inline OperatorCallSite siteOfNode(
    camel::runtime::GCGraph *graph, camel::core::context::Frame &frame,
    camel::runtime::gc_node_ref_t node) {
    GUARD(graph != nullptr, "an operator ran in a frame that has no runtime graph");
    std::shared_ptr<const camel::runtime::ExecPlan> owner = camel::runtime::execPlanOf(*graph);
    return OperatorCallSite{*owner, frame, node, owner};
}

/// Runs `call`, then the rows the plan has at the call's position, and answers what the operator
/// produced. A back end that runs bytecode has not published the value into its slot yet (it does
/// so after this returns), so the value the callable answers is handed to the rows rather than read
/// back out of the frame. The rows run only when the call returns: a fault leaves the count where
/// the call left it, which is what every back end did before there was one place.
///
/// Every node kind a back end runs as a node reaches its position through here except two: an
/// operator, which is `callOperator` above (the same function, named for the kind), and a LOOP
/// node, whose position is run once per round and whose rows are its exit's --
/// `applyRefEntriesForLoopExit`, reached from `loopEnd` by every back end.
template <typename Run> slot_t runNode(const OperatorCallSite &site, Run &&run) {
    const slot_t produced = std::forward<Run>(run)();
    camel::runtime::applyRefEntriesForNode(site.plan, &site.frame, site.node, produced);
    return produced;
}

/// Runs `call`, then the rows the plan has at the call's position, and answers what the operator
/// produced. The operator kind's name for `runNode`: it is the kind that reaches its position by
/// calling an operator.
template <typename Call> slot_t callOperator(const OperatorCallSite &site, Call &&call) {
    return runNode(site, std::forward<Call>(call));
}

} // namespace camel::execute