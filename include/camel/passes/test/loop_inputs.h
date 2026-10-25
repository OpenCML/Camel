// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only: for every LOOP of a program, the values its condition and body regions read from
 * outside them, as `LoopView::externalInputs` gives them. One line per loop carries the shape of
 * the node (its With inputs are the results of its regions and the round port, never a capture)
 * and the two lists: the captures (the default, no constants) and the constants that were read
 * but left out. The plan then pins that a constant is not a capture and that a loop-invariant
 * value is one (design principles 3.10: a constant is not a capture).
 *
 *   LOOP_INPUTS graph=f loop=12 kind=for-while state=1 with=3 captures=1 constants=1
 *     input PORT constant=0 type=int
 *     constant DATA constant=1 type=int
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kLoopInputsTestPass = "test::loop_inputs";
class LoopInputsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};