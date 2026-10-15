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
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kFactStoreTestPass = "test::facts";

class FactStoreTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
