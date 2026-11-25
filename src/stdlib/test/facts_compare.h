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
 *   FACTS_COMPARE kind=core.arrangeable readers nodes=<n> agree=<n> diff=<n> no_fact=<n>
 *   FACTS_COMPARE readers_kind kind=<name> nodes=<n> both_free=<n> both_kept=<n>
 *       kind_free_pred_kept=<n> kind_kept_pred_free=<n> no_fact=<n>
 *   FACTS_COMPARE kind=core.arrangeable draft graphs=<n> nodes=<n> same=<n> diff=<n>
 *       no_fact=<n> no_store=<n> kept=<n> stale=<n>
 *
 * The `readers` line compares the fact with the predicate a pass asks
 * (`runtime::nodeIsFreelySchedulable`); the `draft` line asks the same kind of the graph the
 * program's version decodes into -- the graph an editing session is building -- and compares it
 * with the frozen answer at the same node. `no_store` counts drafts the framework answered no
 * store for (the kind is not askable where only a draft is at hand), `kept` the nodes whose second
 * ask was answered from what the store kept, and `stale` the graphs where an edit to the draft
 * dropped the stored answer. A zero `no_store` and a `kept` equal to `nodes` is what "the
 * framework holds a draft" reads as.
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
 *
 * The two `readers` families compare `core.arrangeable` with the predicate a pass asks before it
 * drops, moves, merges or reorders a node (`runtime::nodeIsFreelySchedulable`). They ask one
 * question, so an answer they differ on is a node with two answers, and the second family is the
 * table that says *which* node kinds: one line per node kind the program carries, counting the four
 * combinations of the two answers -- both say freely arrangeable (`both_free`), both refuse
 * (`both_kept`), and the two disagreements (`kind_free_pred_kept`, `kind_kept_pred_free`) -- plus
 * the nodes the kind answered nothing for. A kind named here with `nodes=0` is absent; a kind whose
 * line is missing was not in the program at all, which is why a case that reads the table asks for
 * the kinds it expects by name rather than only for a zero on the disagreements.
 *
 * Every family walks the nodes the view calls nodes (`graph::GraphView::contains`), not the places of
 * the graph's own numbering: a JOIN holds a place there, and the fact layer refuses an anchor on one
 * ("node N is not a node of its graph"), so counting those places made the pass give up with an
 * internal error on every program that carries a branch -- and a program it gave up on is a program
 * no reading covered.
 *
 * A call is one of those `no_old` nodes and the one the `readers` comparison found an argument
 * about: the kind used to walk a callee's body and answer 1111 for a call to a pure function,
 * while the predicate answered 0 (a call is an anchor that may be an effect). The kind declines at
 * a call now (facts.cpp, ArrangeWalk), so the two readers answer the same and this line reads
 * `diff=0` on `test/cases/facts/two_functions.cml` as well.
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