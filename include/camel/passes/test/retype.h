// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only passes for the graph rebuild primitive (camel/runtime/retype.h).
 *
 * `test::retype_f64_i64` -- the trivial substitution: every `float` on the graphs becomes an
 * `int`, the rebuild must keep the node kinds, the node and edge counts and the topology, the
 * commit's own validators must accept the result, and the graph the program was built from must
 * be exactly what it was. Prints `RETYPE ...` lines for the plan; the program then runs on
 * whatever back end the pipeline names, so the same lines and the same output on four of them is
 * the agreement the plan asserts.
 *
 * `test::retype_enum` -- the probe the defunctionalization step needs: a graph whose value is a
 * `(float, (float) => float)` pair -- a value and the pullback that goes with it -- is rebuilt
 * with that tuple replaced by an `enum` type that has one variant carrying the same tuple. Only
 * types change; no node is added, removed or rewritten. The commit's validators are what proves
 * the JOIN of a branch and its two arms, the signature of the function and the call that reads it
 * all agree after the swap: a substitution that reached only some of them is refused there.
 */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kRetypeTrivialTestPass = "test::retype_f64_i64";
class RetypeTrivialTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

inline constexpr const char *kRetypeEnumTestPass = "test::retype_enum";
class RetypeEnumTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};