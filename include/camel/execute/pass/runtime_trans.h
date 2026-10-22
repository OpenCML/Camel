// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "trans.h"

class RuntimeGraphTranslatePass : public GraphTranslatePass {
  public:
    RuntimeGraphTranslatePass(const camel::core::context::context_ptr_t &ctx)
        : GraphTranslatePass(ctx) {};
    ~RuntimeGraphTranslatePass() override = default;
};
