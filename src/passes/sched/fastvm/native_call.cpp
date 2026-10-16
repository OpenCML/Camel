/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
