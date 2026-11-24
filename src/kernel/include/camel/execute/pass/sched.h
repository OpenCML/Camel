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

/// The scheduling pass that produces no effect: it discards the graph and executes nothing, so the
/// pipeline ends here without a run (design principles 9: a scheduling pass answers with the
/// residual graph or an empty one; this one answers with an empty one and does no work).
class NopPass : public GraphSchedulePass {
  public:
    NopPass(const camel::core::context::context_ptr_t &ctx) : GraphSchedulePass(ctx) {};
    virtual ~NopPass() = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
