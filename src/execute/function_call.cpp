/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Nesting is thread-local; Context is shared by parallel workers and must not own a callback.
#include "camel/execute/function_call.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/graph.h"
#include <utility>
namespace camel::execute {
namespace {
thread_local FunctionCallScope *currentScope = nullptr;
}
FunctionCallScope::FunctionCallScope(core::context::Context &context, FunctionInvoker invoker)
    : context_(&context), invoker_(std::move(invoker)), previous_(currentScope) {
    currentScope = this;
}
FunctionCallScope::~FunctionCallScope() { currentScope = previous_; }
slot_t invokeFunction(
    core::context::Context &context, Function *function, std::span<const slot_t> withArguments,
    std::span<const slot_t> normalArguments) {
    auto *scope = currentScope;
    if (!scope || scope->context_ != &context || !scope->invoker_)
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::RuntimeError,
            "native subgraph invocation requires a callsGraphs execution scope");
    if (!function || !function->graph() ||
        function->graph()->withPorts().size() != withArguments.size() ||
        function->graph()->normPorts().size() != normalArguments.size())
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::RuntimeError,
            "native subgraph invocation has an invalid function or argument count");
    return scope->invoker_(function, withArguments, normalArguments);
}
} // namespace camel::execute
