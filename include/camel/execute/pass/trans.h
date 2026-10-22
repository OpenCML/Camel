// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "base.h"

class GraphTranslatePass : public GraphIRPass {
  public:
    GraphTranslatePass(const camel::core::context::context_ptr_t &ctx) : GraphIRPass(ctx) {};
    virtual ~GraphTranslatePass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override = 0;
};
