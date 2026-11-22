// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "executor.h"
#include "camel/execute/executor.h"
#include "operators.h"

namespace {

class ReExecutor : public Executor {
  public:
    ReExecutor(
        camel::core::context::context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : Executor(std::move(ctx), std::move(ops)) {}
};

} // namespace

executor_ptr_t createReExecutor(camel::core::context::context_ptr_t ctx) {
    return std::make_shared<ReExecutor>(std::move(ctx), getReOpsMap());
}
