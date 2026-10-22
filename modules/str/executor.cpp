// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "executor.h"
#include "camel/execute/executor.h"
#include "operators.h"

namespace {

class StrExecutor : public Executor {
  public:
    StrExecutor(
        camel::core::context::context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : Executor(std::move(ctx), std::move(ops)) {}
};

} // namespace

executor_ptr_t createStrExecutor(camel::core::context::context_ptr_t ctx) {
    return std::make_shared<StrExecutor>(std::move(ctx), getStrOpsMap());
}
