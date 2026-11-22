// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Synchronous native-operator calls through the currently executing backend.

#pragma once
#include "camel/core/rtdata.h"
#include <functional>
#include <span>
class Function;
namespace camel::core::context {
class Context;
}
namespace camel::execute {
using FunctionInvoker =
    std::function<slot_t(Function *, std::span<const slot_t>, std::span<const slot_t>)>;
class FunctionCallScope {
  public:
    // An empty invoker masks an outer scope for an operator without callsGraphs.
    FunctionCallScope(core::context::Context &context, FunctionInvoker invoker);
    ~FunctionCallScope();
    FunctionCallScope(const FunctionCallScope &)            = delete;
    FunctionCallScope &operator=(const FunctionCallScope &) = delete;

  private:
    friend slot_t invokeFunction(
        core::context::Context &, Function *, std::span<const slot_t>, std::span<const slot_t>);
    core::context::Context *context_;
    FunctionInvoker invoker_;
    FunctionCallScope *previous_;
};
// Inputs must remain rooted until binding. Store/root the result before a safepoint.
// Only legal inside the current thread's declared callsGraphs native operator.
slot_t invokeFunction(
    core::context::Context &context, Function *function, std::span<const slot_t> withArguments,
    std::span<const slot_t> normalArguments);
} // namespace camel::execute
