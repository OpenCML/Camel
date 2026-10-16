/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY passes over the graph facts (camel/runtime/facts.h). They can only be named when
 * the environment variable CAMEL_ENABLE_TEST_PASSES is set (fault.h). Each prints one line per
 * check, `FACTS <check> <result>`.
 *
 * `test::facts` exercises the registry and the store on their own: the registration rules (one
 * rejected kind per rule; the `tiles.` prefix reserved to the module tiles, registered the way
 * a module does), the scopes, the classes (a GraphContent fact is read from the graph and cannot
 * be stored; a Target fact needs its target), typed access, a cross-graph fact, the validator;
 * then a commit that edits only `main`: facts on `main` (and on the root, which refers to it)
 * read stale, facts on graphs below it stay valid, a Cone fact whose node did not change is
 * revalidated; then a new operator declaration: facts that read declarations go stale, the
 * others stay. It needs a program whose `main` calls a function `f` that does not call `main`.
 * It returns the committed program.
 *
 * `test::facts_seed` and `test::facts_check` bracket other passes (rewrites interact with facts,
 * design 10.5): `camel p.cml test::facts_seed std::inline test::facts_check`. The first computes
 * every core fact of the program and prints how many of each kind (`FACTS seeded <kind> <n>`).
 * The second reads every stored fact against the program as it is now and prints, per graph,
 * how many are valid, revalidated (Cone) and stale, and how many are about graphs the program
 * no longer has (`FACTS graph <name> valid=.. revalidated=.. stale=..`, `FACTS gone <n>`); then
 * it computes again what was stale and verifies every stored fact against a fresh computation
 * (`FACTS verify equal=<n> differ=<n>`): a pass's rewrite is never reused silently. When the
 * program has loops it also lowers them privately, as std::fvm does, and reads the facts on
 * the copy (`FACTS lowered <name> ...`): the graph that held the loop is new, the others are not.
 *
 * `test::facts_persist` exports the program's facts as a cache would (fact_persist.h), checks
 * that each line of text reads back to the same fact, loads them into an empty store (every
 * fact stored or matching the graph, and equal to a fresh computation), then edits `main`
 * (adds an unused constant) and loads them again: the facts about `main` and the graphs that
 * refer to it are refused (`graph changed`), the others are stored.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kFactStoreTestPass = "test::facts";

inline constexpr const char *kFactSeedTestPass    = "test::facts_seed";
inline constexpr const char *kFactCheckTestPass   = "test::facts_check";
inline constexpr const char *kFactPersistTestPass = "test::facts_persist";

class FactStoreTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

class FactSeedTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

class FactCheckTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

class FactPersistTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
