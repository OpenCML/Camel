// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only: build the graph shape that needs a node kept alive (`:op/keep`), and read what a pass
 * list's rewrite left in the value.
 *
 * A pass may not write an effect the program did not have: the pass boundary check refuses one with
 * `RuntimeDiag::PassBoundaryResources` ("changed the ambient resources the program touches: before
 * nothing, after stdout"). So a test pass cannot put a print into a graph to make a shape -- it has
 * to take a print the source wrote and arrange the nodes around it. That is why `test::keepshape`
 * rewrites the graph of a function that is no `sync` block (the case writes the print there, so
 * nothing in that graph has a sync edge) and removes the node that reads the print, rather than
 * building a program of its own. The shape is the one a block's chain cannot keep, so `replaceNode`
 * has to keep the print alive through data.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kKeepShapeTestPass   = "test::keepshape";
inline constexpr const char *kKeepInspectTestPass = "test::keepinspect";

/// Removes the node that takes a print's last read, in the graph of a function that is no `sync`
/// block, and prints what the rewrite left in the value: the operator, the data use, the wait.
class KeepShapeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/// Counts the `:op/keep` nodes of a program without changing it, and how many of them keep a node
/// that stands in a `sync` block's chain -- the two shapes the operator is written in.
class KeepInspectTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};