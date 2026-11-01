// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only: build the one graph shape `:op/keep` is for, and say what the rewrite did with it.
 *
 * No pass of the compiler writes the shape today -- an effect a pass takes the last read of is a
 * statement of a `sync` block in the graphs the builder writes, so the block's chain keeps it --
 * and the operator is therefore not reachable from any program the compiler builds. This pass
 * writes the shape in the entry graph of a real program instead: a print whose value one node
 * reads, that node read by another print, and no `sync` edge anywhere, so `replaceNode` has to
 * keep the print alive through data rather than through the block's chain. Both readings a plan
 * can make come from one program: the line the print writes (does the effect still run) and the
 * line the pass writes (is the operator in the value, and what keeps the effect alive).
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kKeepShapeTestPass = "test::keepshape";

class KeepShapeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};