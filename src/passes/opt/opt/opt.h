// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "../inline/config.h"
#include "camel/execute/pass/opt.h"

class OptimizeRewritePass : public RuntimeGraphRewritePass {
  public:
    OptimizeRewritePass(
        const camel::core::context::context_ptr_t &ctx, const OptimizeRewriteConfig &config = {})
        : RuntimeGraphRewritePass(ctx), config_(config) {}
    ~OptimizeRewritePass() override = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    OptimizeRewriteConfig config_{};
};
