// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only producer of a boundary violation of one class: a pass that keeps every ambient resource
 * name the program had and changes only the way it is used -- `stdout` written where the program
 * wrote it, `stdout` read where nothing reads it. `test::bounds_flip` does that to the print of the
 * program it is given; the reading is `PassBoundaryResources` (src/execute/pass.cpp), which has to
 * name the resource and the two ways.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kBoundsFlipTestPass = "test::bounds_flip";

class BoundsFlipTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

