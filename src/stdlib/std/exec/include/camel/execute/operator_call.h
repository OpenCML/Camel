// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The one place a node's rows are run, and the one place an operator is called.
 *
 * Four back ends call operators -- std::nvm, std::fvm, std::jit, and the taskflow scheduler -- and
 * each of them used to apply the count's rows itself, at every place a call can happen. The places
 * that forgot are exactly the places the count went missing: the SCHD instruction in gotovm.cpp and
 * fastvm.cpp's interpreter exit, and the Oper branch of TaskflowExecSchedPass::executePreparedNode.
 * A list that has to be remembered at each call site is
 * how that happens twice; here the list is one call, so no back end has a place left to forget it
 * and no back end has a place left to add a second one.
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

#include <utility>

namespace camel::execute {

/// Where one operator call stands in the plan it runs in: `node` is the operator node the plan laid
/// the rows out for, and `frame` is the frame the call reads and writes.
struct OperatorCallSite {
    const camel::runtime::ExecPlan &plan;
    camel::core::context::Frame &frame;
    camel::runtime::gc_node_ref_t node;
};

/// The site of a bytecode call: bytecode holds the instruction's destination slot where the node
/// form holds the node, and the plan's own table is the translation (`refNodeOfSlot`) -- one a back
/// end must not write for itself, or two of them would disagree about which position an instruction
/// is. A slot no operator makes answers `kInvalidNodeRef`, which is a position with no rows.
inline OperatorCallSite
siteOfSlot(camel::core::context::Frame &frame, camel::runtime::gc_data_idx_t result) {
    const auto *graph = frame.graph();
    GUARD(graph != nullptr, "an operator ran in a frame that has no runtime graph");
    const camel::runtime::ExecPlan &plan = graph->execPlan();
    return OperatorCallSite{plan, frame, plan.refNodeOfSlot(result)};
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