// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "sched.h"

class RuntimeGraphSchedulePass : public GraphSchedulePass {
  public:
    RuntimeGraphSchedulePass(const camel::core::context::context_ptr_t &ctx)
        : GraphSchedulePass(ctx) {};
    ~RuntimeGraphSchedulePass() override = default;
};
