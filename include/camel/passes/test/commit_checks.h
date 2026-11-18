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
 * The order-completeness half (analysis 4.1, second item) is read by the three passes below.
 *
 * The pass is not in a default pipeline: naming it is the whole switch.
 */

#pragma once
#include "camel/execute/pass/opt.h"

/// The three transformations the two commit checks are read on: one per check, and one that must
/// pass.
///
/// The defect is not in the pass's own reading of a program but in what it leaves behind for the
/// commit to publish, so each of these says which edit it made and then commits it, and says nothing
/// more: the commit is the reader, and the diagnostic it refuses with is what a plan compares.
///
///   `test::commit_order` splices a fresh write to the resource the program already writes, and
///   orders it against nothing, so the second check must refuse it and name the pair of nodes;
///   `test::commit_effect` splices a write into a graph whose type declares none, so the first
///   check must refuse it and name the node;
///   `test::commit_legal_splice` makes the same splice as the first but ordered after the write that
///   was already there, in a graph whose type declares the write, so nothing is refused and the
///   program runs. Without it, "it was refused" would mean only "it is always refused", which is a
///   reading of neither check.
///
/// All three ask the checks whatever the build asks (`camel::runtime::setCommitChecksEnabled`,
/// camel/runtime/resources.h): a shipped build does not ask them of a rewrite that has only the graph
/// module's interface, and what a plan reads has to be the same in every build the plans run in. Each
/// one prints the build's own answer first, so the asked run is on the record beside the answer it
/// overrides.
inline constexpr const char *kCommitOrderTestPass = "test::commit_order";
class CommitOrderTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

inline constexpr const char *kCommitEffectTestPass = "test::commit_effect";
class CommitEffectTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

inline constexpr const char *kCommitLegalSpliceTestPass = "test::commit_legal_splice";
class CommitLegalSpliceTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

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