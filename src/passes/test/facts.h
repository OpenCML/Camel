// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY passes over the graph facts (camel/runtime/facts.h). They can only be named when a
 * test pipeline names them (fault.h). Each prints one line per check, `FACTS <check> <result>`.
 * `test::facts` exercises the registry and the store on their own: the registration rules (one
 * rejected kind per rule; the `tiles.` prefix reserved to the module tiles, registered the way a
 * module does), the scopes, the classes (a GraphContent fact is read from the graph and cannot be
 * stored; a Target fact needs its target), typed access, a cross-graph fact, the validator; then a
 * commit that edits only `main`: facts on `main` (and on the root, which refers to it) read stale,
 * facts on graphs below it stay valid, a Cone fact whose node did not change is revalidated; then
 * a new operator declaration: facts that read declarations go stale, the others stay. It needs a
 * program whose `main` calls a function `f` that does not call `main`. It returns the committed
 * program. `test::facts_seed` and `test::facts_check` bracket other passes (rewrites interact with
 * facts, design 10.5): `camel p.cml test::facts_seed std::opt::inline test::facts_check`. The
 * first computes every core fact of the program and prints how many of each kind (`FACTS seeded
 * <kind> <n>`), the declaration epoch it stamped them with (`FACTS epoch seed=<n>`) and each
 * graph's revision (`FACTS seed revision <graph> <n>`). The second prints the epoch and revisions
 * it finds (`FACTS epoch check=<n>`, `FACTS check revision <graph> <n>`), then reads every stored
 * fact against the program as it is now and prints, per graph, how many are valid, revalidated
 * (Cone) and stale, per kind on a graph how many of that kind are (`FACTS graph kind <graph>
 * <kind> valid=.. revalidated=.. stale=..`), and how many are about graphs the program no longer
 * has (`FACTS graph <name> valid=.. revalidated=.. stale=..`, `FACTS gone <n>`). It then computes
 * again what was stale, reads every fact a consumer could still read (its graphs and nodes exist)
 * and prints `FACTS after recompute valid=.. stale=.. gone=<n>`: no readable fact may be stale
 * after the recompute. Finally it verifies every stored fact against a fresh computation (`FACTS
 * verify equal=<n> differ=<n>`): a pass's rewrite is never reused silently. Comparing the two
 * epochs and the two sets of revisions says *why* a fact went stale: a new revision means the
 * graph changed, a new epoch means a declaration did. When the program has loops it also lowers
 * them privately, as std::fvm does, and reads the facts on the copy (`FACTS lowered <name>...`):
 * the graph that held the loop is new, the others are not. `test::facts_load_rules` loads the
 * `rules` library (and through it `math`) before the facts are seeded, and prints the declaration
 * epoch before and after (`FACTS rules_loaded epoch <before> -> <after>`). It returns the program
 * unchanged. It is the control for measuring the cost of invalidation by the declaration epoch:
 * the same pipeline without it lets `std::opt`'s algebra stage load `rules` after the facts exist,
 * so those facts go stale although their graphs did not change. `test::facts_persist` exports the
 * program's facts as a cache would (fact_persist.h), checks that each line of text reads back to
 * the same fact, loads them into an empty store (every fact stored or matching the graph, and
 * equal to a fresh computation), then edits `main` (adds an unused constant) and loads them again:
 * the facts about `main` and the graphs that refer to it are refused (`graph changed`), the others
 * are stored. `test::facts_node` is the node-level rule (Dependency::Node, design 4.4): it stores
 * a fact about one node of the root twice, once for a kind of that rule and once for a kind of the
 * conservative rule (Anchors), adds a node to the root and commits; the first reads valid, the
 * second stale. Then it changes the node itself and commits: the first reads stale too (`FACTS
 * node_other node=valid graph=stale`, `FACTS node_self node=stale`).
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kFactStoreTestPass = "test::facts";

inline constexpr const char *kFactLoadRulesTestPass = "test::facts_load_rules";

inline constexpr const char *kFactSeedTestPass    = "test::facts_seed";
inline constexpr const char *kFactCheckTestPass   = "test::facts_check";
inline constexpr const char *kFactPersistTestPass = "test::facts_persist";
inline constexpr const char *kFactNodeTestPass    = "test::facts_node";

class FactStoreTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class FactLoadRulesTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class FactSeedTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class FactCheckTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class FactPersistTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class FactNodeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
