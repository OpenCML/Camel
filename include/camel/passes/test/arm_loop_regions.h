// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only: the commit-time check that a loop's regions are regions of where the loop is
 *  (docs/technical/17_graph_rewrite_api_contracts.md, 4.1, and). It takes a program whose loop
 * is in a branch arm and gives that loop a second body region in the body of the graph, so that
 * the body of the loop is no longer a region of the arm; the commit must refuse it, and the pass
 * prints the message. A camel source program cannot build this graph, so the test of the check is
 * a pass that edits the graph through the draft API.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kArmLoopRegionsTestPass = "test::arm_loop_regions";
class ArmLoopRegionsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};