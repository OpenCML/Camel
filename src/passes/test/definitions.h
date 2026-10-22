// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY passes over the definition graphs of operators (camel/runtime/definitions.h). They
 * are named by a test pipeline; no environment variable gates them (fault.h).
 *
 * `test::definitions` prints one line per check, `DEFINITION <check> <result>`: the definitions
 * the tensor module declares (key, the function's graph, its parameters and content hash);
 * the registration check (a definition without a function is rejected); and one definition
 * refused per check made when it is built, each declared here on a tensor operator from the
 * library `bad_definitions` next to the program: a cycle through two definitions (exp defined
 * with softmax, which is defined with exp), a definition that reaches its own operator, a
 * function of the wrong arity, a definition with an effect for a pure operator, a missing
 * function, a generic one, a library that does not compile. Then it asks twice for a
 * definition (built once) and expands nothing. The program's graph is returned unchanged.
 *
 * `test::expand_definitions` expands every OPER node, of every graph of the program, whose
 * operator declares a definition (expandDefinition, inlined) and commits: the program then
 * computes with the definitions instead of the kernels. It reports on stderr, so that the
 * program's own output can be compared with a run without it: one line per operator,
 * `EXPANDED <uri> <count>`, and one per node it left, `NOT EXPANDED <uri>: <why>`.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kDefinitionsTestPass       = "test::definitions";
inline constexpr const char *kExpandDefinitionsTestPass = "test::expand_definitions";

class DefinitionsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class ExpandDefinitionsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
