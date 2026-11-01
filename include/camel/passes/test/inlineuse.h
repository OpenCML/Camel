// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only readings of the ambient use a program keeps after a pass copies its code.
 *
 * `test::inline_use` prints every standard ambient use the program still names, so a run can show
 * that inlining a function into its caller moved the print's node without dropping what it does.
 * `test::inline_use_drop` does the opposite on purpose: it erases the print's node, so the reading
 * afterwards names one resource fewer and the pass boundary (`RuntimeDiag::PassBoundaryResources`)
 * has something to refuse. One measures, the other breaks, and the plan pins both.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kInlineUseTestPass     = "test::inline_use";
inline constexpr const char *kInlineUseDropTestPass = "test::inline_use_drop";

/// Prints the program's standard ambient uses: one line per (resource, way), sorted.
class InlineUseTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/// Erases the node of every `:io/println`, which takes the program's write of `stdout` with it.
class InlineUseDropTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};