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

/**
 * Reads the same defect through the exemption every pass written in Camel has, in one run:
 * the program the fold above makes is handed to a pass written in Camel (`opt::cse`) with the
 * commit checks asked of such a pass exactly as this build asks them, and -- when that answer was
 * "not asked" -- once more with them asked regardless
 * (`camel::execute::setCamelPassCommitChecksEnabled`, declared in camel/execute/pass/base.h).
 *
 * The two readings are one pair and neither stands alone. A shipped build prints
 * `CAMEL_CHECKS build asks=0`, then `CAMEL_CHECKS accepted, asked=0`, then `CAMEL_CHECKS released:
 * ...` (the run went on past the pass, so nothing was checked), then `CAMEL_CHECKS asked=1 -> the
 * same program, the same pass`, and then the containment diagnostic and no third line: at once
 * that the check is skipped where it is not paid for, that it is not vacuous where it is asked,
 * and that the only difference between the two is the request. The debug build asks by itself, so
 * its first line is `CAMEL_CHECKS build asks=1` and the first run is refused, which is the whole
 * reading there; `test/plans/debug/camel_pass_commit_checks.plan.toml` is the plan that reads it
 * there.
 *
 * A refusal records its diagnostic and returns; it does not throw. The run ends at it one step
 * later, where the pipeline reads the recorded errors after this pass (`applyPassesDetailed`), so
 * the second answer is announced before the second run and not confirmed after it: the third line
 * is printed only when the second run came back with a program. A check that has stopped refusing
 * therefore leaves `CAMEL_CHECKS asked=1 -> accepted` as its trace, which is red on purpose.
 *
 * This pass returns the program it was given, not the one the fold made: an uncontained program is
 * what the fixture puts in front of the pass under test, and handing it on would have the boundary
 * check refuse the fixture before the pass under test is read at all.
 */
inline constexpr const char *kCamelCommitChecksTestPass = "test::camel_commit_checks";
class CamelCommitChecksTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};