// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "executor.h"
#include "../tensor/ops/registry.h"
#include "optim.h"

namespace {

class NnExecutor : public Executor {
  public:
    NnExecutor(
        camel::core::context::context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : Executor(std::move(ctx), std::move(ops)) {}
};

} // namespace

executor_ptr_t createNnExecutor(camel::core::context::context_ptr_t ctx) {
    auto kernels = camel::nn::optimizerKernels();
    kernels.merge(camel::tensor::ops::OpRegistry::instance().kernelMap("nn"));
    return std::make_shared<NnExecutor>(std::move(ctx), std::move(kernels));
}
