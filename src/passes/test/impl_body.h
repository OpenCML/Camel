// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::impl` expands the semantic bodies of the program's `#impl` functions
 * (the consumption side of kGraphFlagImplBody / Program::implBodies, design principles 10.4)
 * and prints a line per graph and per node, so a test can compare the graph a body expands to
 * with the graph the same body compiles to without the tag. Nothing is inlined or rewritten:
 * the program is returned unchanged.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kImplBodyTestPass = "test::impl";

class ImplBodyTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};