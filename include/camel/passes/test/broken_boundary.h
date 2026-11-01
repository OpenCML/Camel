// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Two test-only passes that hand the pipeline a graph it must refuse: one loses the exit node of
 * the program's own graph, the other adds an operator node nothing declares. Each stands for a
 * class of mistake a pass can make -- a rewrite that replaces or removes the node a program returns
 * its value at, and one that leaves an operator node no call can run -- and both are caught by the
 * check every pass ends with (src/execute/pass.cpp, `entryExitViolation` and
 * `portDeclarationViolation`). Neither is in a default pipeline: naming one is the whole switch.
 */

#pragma once
#include "camel/execute/pass/opt.h"

/// Erases the exit node of the program's root graph and returns the program.
inline constexpr const char *kBreakExitTestPass = "test::break_exit";
class BreakExitTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/// Adds an operator node whose operator no declaration names.
inline constexpr const char *kBreakPortsTestPass = "test::break_ports";
class BreakPortsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};