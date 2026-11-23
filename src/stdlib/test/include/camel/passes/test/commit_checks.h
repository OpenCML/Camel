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
/// All three print what the draft added before they commit it. The tier of the checks is what the
/// draft added, not what the build is (`commitAddsAccess` in
/// camel/runtime/resources.h), and each of these drafts adds an access, so each one is asked in every
/// build the plans run in -- the line is on the record so that a run which was not asked cannot look
/// like one that was.
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

/// Retypes one call site twice, through `materializeStaticValue`, and reports the type the encoded
/// program carries: the second materialization is what a pass does in a round after the first, and a
/// value that keeps the type of the round before it is a call site that lags its callee.
inline constexpr const char *kValueRetypeTwiceTestPass = "test::value_retype_twice";
class ValueRetypeTwiceTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/**
 * The four readings asked for beyond the two of analysis 4.1. Each
 * one writes the one thing that check is about, commits it, and prints what the commit answered; the
 * plan compares the answer.
 *
 *   `test::commit_exit_region` moves the graph's exit into one of a branch's arms, which is the edit
 *   an inliner makes when it puts an identity's body where the exit read it (the calculus's
 *   counter-example, `test/cases/opt/commit_checks/identity_at_exit.cml`). Nothing needs an arm that
 *   the graph does not enter, so a value read out of one is a read that never happens: the commit
 *   must refuse it. The check lives in the seal (`validateRuntimeGraphPayloadImpl`,
 *   src/runtime/graph.cpp) and not in the two checks below, because it is a property of the graph
 *   published and not of what the commit added -- so the refusal here is the seal's own, and it is
 *   asked in every build and of every commit.
 *
 *   `test::commit_cycle` makes one node read itself. The draft's input setters do not look for a
 *   cycle -- they cannot, a cycle is composed out of several rewrites -- so the commit is where the
 *   seal's own acyclicity judgement reads the graph, and it must refuse it there.
 *
 *   `test::commit_ancestor` splices a write into one arm of a branch whose outer region already
 *   writes the same resource, and orders it against nothing. Neither node of the pair out there is
 *   one this commit made -- the branch node and the outer write were the program's -- so a commit
 *   that judged a pair only when one of its nodes was made would publish it. It must be refused, and
 *   the diagnostic must name the branch node and the outer write.
 *
 *   `test::commit_tier` reads the tier itself: the same two drafts, one that adds an access and one
 *   that does not, under both answers the one build cell can hold. The two drafts answer different
 *   things and the cell changes neither -- the rule read as a number rather than
 *   asserted: the tier is what the draft added, and no build can move it.
 */
inline constexpr const char *kCommitExitRegionTestPass = "test::commit_exit_region";
class CommitExitRegionTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

inline constexpr const char *kCommitCycleTestPass = "test::commit_cycle";
class CommitCycleTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

inline constexpr const char *kCommitAncestorTestPass = "test::commit_ancestor";
class CommitAncestorTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

inline constexpr const char *kCommitTierTestPass = "test::commit_tier";
class CommitTierTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/**
 * Reads the half of the tier that "did the draft make a node" cannot see: an edit that makes **no**
 * node and changes the graph all the same. The table of `docs/design/graph-editing/README.cn.md` 8.1
 * makes the tier a fact about *which edit was used*, and its last row is the low-level interface --
 * "add an edge, change an operator" -- which owes all six checks whatever it made. A commit that
 * read only the nodes a draft made asked nothing of such an edit and published it.
 *
 * `test/cases/opt/commit_checks/inplace_edit.cml` is the fixture: two functions that each write the
 * console twice inside one `sync` block. The edit is one and it is the same in both: the second write
 * is taken out of its block's chain (`setCtrlInputs` and `setSyncInputs`, both empty). Nothing is
 * made, nothing is erased, no operator is swapped.
 *
 *   * in `twice` neither write reads the other, so the chain was the only thing ordering them: the
 *     commit must be asked the order check, and that check must refuse it. Before this pass's tier
 *     existed the commit was not asked anything and published two writes to one resource that the
 *     program leaves free to happen in either order.
 *   * in `carries` the second write reads the first one's value, so a data edge orders them with the
 *     chain gone: the same edit, asked the same question, publishes. Without that half "it was
 *     refused" would only mean "an in-place edit is always refused", which is a reading of nothing.
 *   * the third draft reads the same pair with the order turned round: the chain is taken away in
 *     every kind it is carried in, and an order edge says the second write runs before the first,
 *     and the commit must publish. The pair is ordered either way, so a check that refused it would
 *     be fixing the direction the program was written in, which is a meaning a pass may change and
 *     not a promise the check makes.
 *
 * The pass returns the program it was given: the commits are the reading, and what runs next must be
 * the fixture.
 */
inline constexpr const char *kCommitInPlaceTestPass = "test::commit_inplace";
class CommitInPlaceTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/**
 * Reads the one question about a read across regions that the *port* rule of the seal
 * (`checkGraphCrossReferences`, src/runtime/graph_edges.cpp) does not ask, against the rule that
 * does ask it: `validateRuntimeBranchArms` ("a node reads only nodes that run whenever it runs").
 *
 * The shape is one a rewrite can make and a hand-written graph will not: a COPY in one arm of a
 * branch whose value is the *other* arm's, and the branch's JOIN reading the copy where the first
 * arm's own value stood, so the read is on the edge the arm ends at and nothing can be dead about
 * it. `test/cases/opt/commit_checks/sibling_arm.cml` is the fixture, and its two arms compute a
 * value each, because a constant written in an arm is hoisted to the body and has no region to be
 * read out of.
 *
 * The pass makes that read and commits it, then makes the same copy read a value of the arm it sits
 * in and commits that. The two answers are the reading: a refusal that names the node and the two
 * arms, and a publication, which is what says the refusal is about the arm and not about the copy.
 */
inline constexpr const char *kCommitSiblingArmTestPass = "test::commit_sibling_arm";
class CommitSiblingArmTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/**
 * Reads the read across regions that a graph with *no branch* is never asked about. The rule
 * (`validateRuntimeBranchArms`, src/runtime/branch_arms.cpp) is "a node reads only nodes that run
 * whenever it runs", and it is written over the region tree -- so the body of a loop is a region of
 * the graph exactly as the arm of a branch is, and a read of a value the loop's body computes is the
 * same question as a read across two arms. What differed was only whether it was asked: the check
 * returned as soon as a graph had no BRCH, so the whole of a branch-less graph's cross-region reads
 * went unjudged.
 *
 * `test/cases/opt/commit_checks/loop_body_read.cml` is the fixture: a loop whose body computes a
 * value, and no branch anywhere. The first draft is the read that must be refused -- a COPY in the
 * function body (region 0) reading a value of the loop's body, waited on by the write that runs
 * after the loop so that nothing about it is dead. The pass then makes the two reads that must not
 * be refused, because a refusal on either of them would be the check judging the region pair by the
 * wrong end: the same COPY reading a value of its own region, and a COPY inside the loop's body
 * reading a value the function body computed (the direction every loop body in the language writes).
 */
inline constexpr const char *kCommitLoopBodyReadTestPass = "test::commit_loop_body_read";
class CommitLoopBodyReadTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/**
 * Reads the promise a graph makes twice: its ports are the parameters its signature declares.
 *
 * A Port node is the argument a caller passes and `funcType()` is what the caller is told the
 * parameter is, so a graph publishes one statement about one parameter in two places. Nothing read
 * the two together: a call site is checked against the port node, the value the graph hands on is
 * checked against the signature, and the entry arguments are read through the signature. A draft
 * that republishes a graph under *another function's type* -- `GraphDraft::setFuncType`, which is
 * what a fold does when it decides two bodies are one function, and what `test::commit_tier`
 * already reads on two graphs that happen to have the same parameter types -- therefore publishes a
 * graph that says one type to its callers and another to its own body, and every plan stayed green.
 * `test/cases/opt/commit_checks/port_type.cml` is that program: three called functions, two of
 * which take an `int` and one a `float`.
 *
 * The two drafts are one reading. The first republishes a graph under the signature of a graph whose
 * parameter type differs, and the commit must refuse it, naming the parameter, the declared type and
 * the type the port node carries. The second republishes the same graph under the signature of a
 * graph whose parameter types are the same, and the commit must publish it -- without it, "it was
 * refused" would mean only "a signature was replaced", which is a reading of neither rule and would
 * make every legitimately retyped graph unpublishable.
 *
 * The commit asks that of every graph it encodes now
 * (`camel::runtime::validateRuntimeGraphPortTypes`), so this pass reads the refusal the seal gives and
 * not the only place the two lists are compared: a graph the compiler built for itself is asked the
 * same question, which is how the specialization's own shift was found.
 */
inline constexpr const char *kCommitPortTypeTestPass = "test::commit_port_type";
class CommitPortTypeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/**
 * Reads the same promise on the graph no narrower placement could reach: one **this transaction
 * made**.
 *
 * `validateRuntimeGraphPortTypes` is asked of every graph a commit encodes, so this graph owes the
 * answer for the same reason every other one does. Two narrower placements missed it, and neither
 * was the case either was written for -- that case was the compiler's own outputs (`*$spec*`,
 * `main$lift0`, `split_of_jvp_of_*_backward`) whose norm-port lists sat one place over from their
 * signatures. The commit's closing loop
 * decodes a made graph, finds no write recorded, drops it from `edited_`, so a commit that asked
 * only the graphs it edited never asked it (`graph 'made$mismatch' not asked the port question
 * (edited=0, made=1)`); and the signature tier `sameSignature(before, after)` would have waived it
 * too, a made graph having a `before` that is itself, both its nodes and its signature written by
 * the draft, so the comparison can only answer "unchanged".
 *
 * Two drafts, one question, and the two answers are the reading:
 *   * the made graph declares parameter 0 as `i32` and its port node carries an `i32` -- the commit
 *     must publish it, which is what says a made graph is not simply refused for being made;
 *   * the same graph declares parameter 0 as `i32` while its port node carries an `f32` -- the
 *     commit must refuse it, naming the parameter, the declared type and the type the port node
 *     carries. A made graph is wrong from the moment it is made, and this is the moment.
 *
 * The two graphs are encoded by the transaction that makes them and handed to the session through
 * `noteMadeGraph`, the seat `RuntimeGraphDraftSession` gives a graph the edit made (design
 * graph-editing 2.5: the draft of `edit d = f` is `f`, the graphs `f` reaches **and the graphs this
 * edit makes**). The fixture is `test/cases/opt/commit_checks/port_type.cml`, and neither draft
 * touches it: what the program is, is only what `std::nvm` runs after the commits.
 */
inline constexpr const char *kCommitMadeGraphPortsTestPass = "test::commit_made_graph_ports";
class CommitMadeGraphPortsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/**
 * The same reading for the order question: a graph this transaction makes, with two writes to the
 * console in its body and nothing ordering them, then the same two with an edge between them. No
 * program holds either graph, so no list of "nodes the draft changed" can name them -- every node in
 * them is one this transaction put there, and the whole graph is what the commit answers for.
 */
inline constexpr const char *kCommitMadeGraphOrderTestPass = "test::commit_made_order";
class CommitMadeGraphOrderTestPass : public RuntimeGraphRewritePass {
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