// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::facts_compare` reads two landed facts (`core.controlRegions`, `core.arrangeable`)
 * off one program and prints what they answer, item by item:
 *
 *   FACTS_COMPARE kind=core.controlRegions graphs=<n> regions=<n> enclosing_same=<n>
 *       enclosing_diff=<n> chain_same=<n> chain_diff=<n> within_pairs=<n> within_same=<n>
 *       within_diff=<n> nodes_same=<n> nodes_diff=<n>
 *   FACTS_COMPARE kind=core.arrangeable graphs=<n> nodes=<n> same=<n> diff=<n> no_old=<n>
 *       conversions=<n>
 *   FACTS_COMPARE kind=core.arrangeable answers graphs=<n> nodes=<n> free=<n> kept=<n> no_fact=<n>
 *   FACTS_COMPARE answers_kind kind=<name> nodes=<n> free=<n> kept=<n> no_fact=<n>
 *       kept_example=<graph>:<node>|-
 *   FACTS_COMPARE answers_family family=<name> nodes=<n> free=<n> kept=<n> no_fact=<n>
 *       kept_example=<graph>:<node>|-
 *   FACTS_COMPARE answers_row node_kind=<kind> oper=<uri|-> nodes=<n> free=<n> kept=<n>
 *       no_fact=<n>
 *   FACTS_COMPARE answers_rows rows=<n> nodes=<n>
 *   FACTS_COMPARE kind=core.arrangeable draft graphs=<n> nodes=<n> same=<n> diff=<n>
 *       no_fact=<n> no_store=<n> kept=<n> stale=<n>
 *
 * The `answers` lines are `core.arrangeable`'s own answer, asked through the store at every node the
 * view calls a node: `free` is "a pass may arrange it anywhere" -- the conjunction of the four terms
 * of design principles 3.4 -- `kept` is "a pass must leave it where the program wrote it", and
 * `no_fact` is a node the store answered nothing for. The three tables under the total cut the same
 * answers by node kind, by the family the answer comes from, and by node kind and operator together.
 * `kept_example` is the first node of a row the fact kept, so a split row carries a node to look at
 * and not only a count. A row that is absent is a row the program does not carry, which is why a case
 * asks for the rows it expects by name instead of reading a missing line as agreement.
 *
 * The families are the four terms of 3.4 read as places to stand a count, plus the node kinds whose
 * answer is not an operator record's. A family is the *first* term the node fails, in the order 3.4
 * states them, so one node is one row and never two:
 *
 *   the term 3.4 states    the family the row lands in   what that row means
 *   `pure`                 `oper_not_free`               it writes a cell of its own graph
 *   `noExternalEffect`     `oper_not_free`               it reaches outside the program
 *   `cannotFault`          `oper_may_fault`              it may raise a fault where it is written
 *   `terminates`           `oper_not_free`               it may not end
 *   all four hold          `oper`                        the row that is free
 *   --                     `oper_no_record`              an OPER node naming no operator: a FILL or
 *                                                        an ACCS (`graph/gc_graph_view.h`), answered
 *                                                        from the operation the node performs
 *   --                     `value`, `call`, `brch`,      the answer is not an operator record's: a
 *                          `loop`, `gate`                value is computed from what it reads, a
 *                                                        call and a gate are anchors, a branch and a
 *                                                        loop stand for what runs inside them
 *
 * `oper_not_free` is one row for three of the four terms on purpose. A row per term would be four
 * tables where the reader's question is one -- may a pass move this node -- and a node kept for a
 * write, for a reach outside the program, or for an end nobody promised is one row of that question.
 * A caller that needs the exact bit reads the terms themselves: `Arrangeable::all()`,
 * `operatorArrangeable` and `toString` (`camel/facts/arrangeable.h`) carry all four, and that is what
 * a placement decision acts on. The families are a census, not a second answer.
 *
 * There is nothing here to compare `core.arrangeable` with: one question has one answer, and a copy
 * of an older answer kept in a test to agree with would be a second statement of the same answer.
 * What a case pins is what the fact says.
 *
 * Every walk goes over the nodes the view calls nodes (`graph::GraphView::contains`), never over the
 * places of the graph's own numbering: a JOIN holds a place there, and the fact layer refuses an
 * anchor on one ("node N is not a node of its graph"), so counting those places made the pass give up
 * with an internal error on every program that carries a branch -- and a program it gave up on is a
 * program no reading covered.
 *
 * The `draft` line asks the same kind of the graph the program's version decodes into -- the graph an
 * editing session is building -- and compares it with the frozen answer at the same node. `no_store`
 * counts drafts the framework answered no store for (the kind is not askable where only a draft is at
 * hand), `kept` the nodes whose second ask was answered from what the store kept, and `stale` the
 * graphs where an edit to the draft dropped the stored answer. A zero `no_store` and a `kept` equal
 * to `nodes` is what "the framework holds a draft" reads as.
 *
 * The purpose is the one a move of a computation needs: a fact is a new place for an answer, and "the
 * answer is what the model says it is" is a claim about every item of every graph, not about one
 * example. So the run names the items it read (`regions`, `within` pairs, `nodes`, `no_old`), and a
 * reading over an empty set prints zeros instead of passing silently -- `agents/test.md`, a check that
 * reads nothing must be a red, and the counts are what the plan's case pins.
 *
 * `core.controlRegions`. One of the two readings here *is* a comparison, and its baseline is the one
 * the debugger computed for itself: the region table read with `parent` and a kind test, the
 * innermost arm or loop region worked out by walking outward, and "is this region visible from there"
 * answered by walking the same chain (`program_index.cpp` kept it as `regionOuter` and `visibleAt`).
 * It is written out again in `OldRegionWalk` below and *kept* there: it is the baseline this
 * comparison is made against, so it has to outlive the debugger's copy, and a test-only pass is where
 * a baseline belongs. The program's answer is `ControlRegions` -- the reader the debugger asks now --
 * so the counts are what the replacement kept: `enclosing` per region, the parent chain the debugger
 * called `regionOuter`, `within` for every pair of control regions, and the control region of every
 * node.
 *
 * `core.arrangeable`. The `graphs`/`nodes`/`same`/`diff` line asks a second, narrower question beside
 * the census: the fact at every node against the form that answers an operator and nothing else
 * (`operatorArrangeable`, which has no node, so it cannot read a conversion's pair of types or a
 * branch's arms). Every other node -- a value, a port, a branch, a loop, a call -- has no answer to
 * compare with, and those are counted as `no_old`: the fact answers them, and how many of them there
 * are is part of what putting the question into a fact added. A conversion's fault term is a property
 * of the pair of types the node converts between, so the operator's half is restated at the node
 * (`castMayFault`) and `conversions` counts the nodes that took that restatement. `same`/`diff`
 * compare the four terms, so a run whose terms differ in one bit is a `diff` and names itself.
 *
 * The `answers` tables are the reading the plans pin; the `graph`/`no_old` line is the one that says
 * the node answer still refines the operator answer where it must and only where it must.
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