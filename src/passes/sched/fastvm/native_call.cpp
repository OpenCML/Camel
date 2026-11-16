// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Native operators re-enter this VM through the same frames and recursion counters as HOFs.

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/execute/function_call.h"
#include "camel/runtime/exec_plan.h"
#include "fastvm.h"
#include <utility>
slot_t FastVMSchedPass::invokeNativeOperator(const Bytecode &bc, ctx::Frame &frame, size_t pc) {
    const auto oper             = static_cast<camel::core::OperId>(*bc.extra2());
    const auto function         = bc.extra()->func;
    const data_arr_t withArgs   = bc.wargs();
    const data_arr_t normalArgs = bc.nargs();
    ctx::FrameArgsView withView(frame, withArgs);
    ctx::FrameArgsView normalView(frame, normalArgs);
    withView.setResultType(frame.typeAt<type::Type>(bc.result));
    normalView.setResultType(withView.resultType());
    // What a pass recorded about this node (design principles 9.6); the operator's module reads
    // its own keys, the scheduler knows none.
    withView.setAttrs(*bc.operAttrs());
    normalView.setAttrs(*bc.operAttrs());
    camel::execute::FunctionInvoker invoker;
    if (camel::core::OperatorTable::instance().callsGraphs(oper)) {
        invoker =
            [&](Function *callee, std::span<const slot_t> with, std::span<const slot_t> normal) {
                requireNestedCallRoom(&frame);
                const auto site = higherOrderCallSite(callee);
                auto *child     = framePool_.acquire(site.runtimeGraph);
                populateMarkedCallFrame(child, callee, normal, with);
                // callElement increments nestedCalls_ before entering compiled/interpreted code.
                // Restore it on faults as well, so an outer isolation boundary can continue.
                struct RestoreDepth {
                    size_t &depth;
                    size_t before;
                    ~RestoreDepth() { depth = before; }
                } restore{nestedCalls_, nestedCalls_};
                return callElement(site, child);
            };
    }
    camel::execute::FunctionCallScope scope(*context_, std::move(invoker));
    // The anchor's site for every back end that runs bytecode -- std::fvm's two interpreter loops,
    // std::jit's native operator call, and a std::prl graph that was handed to the interpreter as
    // an image: they all reach the operator here, so the question is asked once rather than in each
    // of them. A `Ref` box made while this operator ran has to be a value the plan names the node of
    // this instruction as producing, and the node is found through the counted-site table the
    // compile kept with the plan, because bytecode has a pc here and not a node. Asked only while
    // --gc-check wants the counted blocks, bracketed on this thread (a scheduler may run several),
    // and reported only: the value has already been made and is returned either way.
    const bool anchorWanted = camel::core::mm::allocatorChecks().printForeignResources;
    camel::runtime::CountedBoxesBracket boxes;
    if (anchorWanted) {
        boxes.begin();
    }
    slot_t result = function(withView, normalView, *context_);
    if (anchorWanted) {
        camel::runtime::noteCountedCreationAt(frame.graph(), pc, boxes.sinceBegin());
    }
    // The count's rows at this instruction's position -- the same site the anchor above is asked at,
    // because it is the one place all three bytecode back ends (std::fvm's two interpreter loops and
    // std::jit's native operator call) reach the operator. Bytecode has a destination slot here and
    // not a node, so the node comes from the plan's own slot table and the entry is the same one the
    // other back ends call; the value the operator just made is handed over rather than read back,
    // because the caller publishes it into the slot after this returns. Run whether or not
    // --gc-check was asked for: counting is what releases a box (design principles 10.5).
    if (const auto *runtimeGraph = frame.graph()) {
        const camel::runtime::ExecPlan &plan = runtimeGraph->execPlan();
        const camel::runtime::gc_node_ref_t node = plan.refNodeOfSlot(bc.result);
        if (node != camel::runtime::kInvalidNodeRef) {
            camel::runtime::applyRefEntriesForNode(plan, &frame, node, result);
        }
    }
    return result;
}
