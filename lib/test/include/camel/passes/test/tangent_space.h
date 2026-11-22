// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only reading of the tangent-space query (`camel/core/tangent_space.h`): one `TANGENT` line
 * per type it asks about, so a test plan can hold the three answers still --
 *
 *   TANGENT type=float space=self
 *   TANGENT type=float[] space=self
 *   TANGENT type=(float, int) space=none
 *   TANGENT type=f16 space=self
 *
 * The pass reads the program and gives it back unchanged: it changes no graph, so any case can
 * carry it. The types it asks about are written here rather than taken from the program, because
 * the answer is a property of the type and the reading has to be the same whatever the program
 * is. */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kTangentSpaceTestPass = "test::tangent_space";

class TangentSpaceTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};