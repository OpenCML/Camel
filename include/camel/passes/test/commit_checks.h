// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A test-only pass that writes what the first commit check of the effect analysis refuses
 * (docs/design/effects/analysis.cn.md 4.1, effect containment), in the shape of a defect E9 found
 * in the C++ passes:
 *
 *   `test::fold_function_type` merges two functions the way a fold that decides two bodies are one
 *   would: it hands the printing function the *type* of the pure one, and leaves the printing body
 *   where it is. Every call site of the printing function now declares `pure` while the body it
 *   runs writes to the console -- a call site reaching further than its type says, which is what
 *   E9's swapped prints came out of. The seal does not catch it: the clauses of section 4.2 are
 *   about the shape of a graph that exists and about the types on an edge, and neither asks what
 *   the body does under the type it publishes.
 *
 * The order-completeness half (analysis 4.1, second item) is not landed here; see the commit that
 * adds this pass for the readings of the existing passes it refuses.
 *
 * The pass is not in a default pipeline: naming it is the whole switch.
 */

#pragma once
#include "camel/execute/pass/opt.h"

/// Gives a function graph that prints the type of one that does not, the same body staying where it
/// is: what a fold that treats two functions as one leaves behind.
inline constexpr const char *kFoldFunctionTypeTestPass = "test::fold_function_type";
class FoldFunctionTypeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};