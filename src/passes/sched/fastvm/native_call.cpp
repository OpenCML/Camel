// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Native operators re-enter this VM through the same frames and recursion counters as HOFs.

#include "camel/core/operator_table.h"
#include "camel/execute/function_call.h"
#include "fastvm.h"
#include <utility>
slot_t FastVMSchedPass::invokeNativeOperator(const Bytecode &bc, ctx::Frame &frame) {
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
    return function(withView, normalView, *context_);
}
