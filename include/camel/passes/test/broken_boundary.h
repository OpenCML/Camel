// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Three test-only passes that hand the pipeline a graph it must refuse. Two are caught by the check
 * every pass ends with (src/execute/pass.cpp, `entryExitViolation` and `portDeclarationViolation`):
 * one loses the exit node of the program's own graph, one leaves an operator port reading a node of
 * no type. The third writes a port at a node the graph does not have at all, which the commit has to
 * refuse before it encodes a version around it -- a reference to nothing cannot be written down.
 * None is in a default pipeline: naming one is the whole switch.
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

/// Points an operator port at a node of no type, which the guard refuses.
inline constexpr const char *kBreakPortsTestPass = "test::break_ports";
class BreakPortsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/// Points an operator port at a node the graph does not have: refused by the commit itself, because
/// a graph version cannot be written around a reference to nothing.
inline constexpr const char *kDanglePortTestPass = "test::dangle_port";
class DanglePortTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/// A pass whose maker refuses to build it. Nothing is wrong with the program: the pass itself
/// declares that it cannot be built, which is the other thing a construction site has to receive --
/// a break while being built, as opposed to a parameter the declaration already refused.
inline constexpr const char *kBrokenMakerTestPass = "test::broken_maker";

/// A pass that ends with an error value and no program (design principles 9.1, `(f: F) => F!`):
/// the value is its answer, and the driver that ran the list has to report it. The case that runs
/// this pass is what makes "the driver reports it" a tested claim rather than a comment.
inline constexpr const char *kFailsWithValueTestPass = "test::fails_with_value";
class FailsWithValueTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/// Points a BRCH node's `join` at a node of the same draft that is not a JOIN. The draft is allowed
/// to be inconsistent while it is written (the draft's `setBrchJoin` does not validate), the encoder
/// is not: `encodeDraftGraph` refuses the graph with the branch and the reference that does not
/// resolve. This is the shape a lift that renumbers a graph can leave behind.
inline constexpr const char *kBreakJoinTestPass = "test::break_join";
class BreakJoinTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
