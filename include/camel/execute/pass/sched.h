// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "base.h"

class GraphSchedulePass : public GraphIRPass {
  public:
    GraphSchedulePass(const camel::core::context::context_ptr_t &ctx) : GraphIRPass(ctx) {};
    virtual ~GraphSchedulePass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override = 0;
};
