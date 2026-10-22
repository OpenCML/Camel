// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "base.h"

class RuntimeGraphRewritePass : public GraphIRPass {
  public:
    RuntimeGraphRewritePass(const camel::core::context::context_ptr_t &ctx) : GraphIRPass(ctx) {};
    virtual ~RuntimeGraphRewritePass() = default;
};
