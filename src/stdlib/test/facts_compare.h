// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::facts_compare` reads a landed fact and the answer the older algorithm gave
 * for the same question, off the same program, one item at a time, and prints the two counts:
 *
 *   FACTS_COMPARE kind=core.controlRegions graphs=<n> regions=<n> enclosing_same=<n>
 *       enclosing_diff=<n> chain_same=<n> chain_diff=<n> within_pairs=<n> within_same=<n>
 *       within_diff=<n> nodes_same=<n> nodes_diff=<n>
 *   FACTS_COMPARE kind=core.arrangeable graphs=<n> nodes=<n> same=<n> diff=<n> no_old=<n>
 *
 * The purpose is the one a move of a computation needs: a fact is a new place for an answer, and
 * "the answer did not change" is a claim about every item of every graph, not about one example.
 * So the run names the items it compared (`regions`, `within` pairs, `nodes`, `no_old`), and a
 * comparison over an empty set prints zeros instead of passing silently -- `agents/test.md`, a
 * check that reads nothing must be a red, and the counts are what the plan's case pins.
 *
 * `core.controlRegions`. The older algorithm is the one the debugger computed for itself: the
 * region table read with `parent` and a kind test, the innermost arm or loop region worked out by
 * walking outward, and "is this region visible from there" answered by walking the same chain
 * (`program_index.cpp` kept it as `regionOuter` and `visibleAt`). It is written out again in
 * `OldRegionWalk` below and *kept* there: it is the baseline this comparison is made against, so
 * it has to outlive the debugger's copy, and a test-only pass is where a baseline belongs. The
 * program's answer is `ControlRegions` -- the reader the debugger asks now -- so the counts are
 * what the replacement kept: `enclosing` per region, the parent chain the debugger called
 * `regionOuter`, `within` for every pair of control regions, and the control region of every
 * node.
 *
 * `core.arrangeable`. The older algorithm answered one node kind and only one: an OPER node's
 * four terms were read from its operator (`operatorArrangeable`). Every other node -- a value, a
 * port, a branch, a loop, a call -- had no answer to compare with, and those nodes are counted as
 * `no_old`: the fact answers them, and how many of them there are is part of what moving the
 * question into a fact added. `same` and `diff` compare the four terms, so a run whose terms
 * differ in one bit is a `diff` and names itself.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kFactsCompareTestPass = "test::facts_compare";

class FactsCompareTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};