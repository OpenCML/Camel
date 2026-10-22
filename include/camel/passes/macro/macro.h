// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/opt.h"

class MacroRewritePass : public RuntimeGraphRewritePass {
  public:
    MacroRewritePass(const camel::core::context::context_ptr_t &ctx)
        : RuntimeGraphRewritePass(ctx) {};
    virtual ~MacroRewritePass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
