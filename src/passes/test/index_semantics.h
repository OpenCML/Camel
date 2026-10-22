// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::index_semantics` exercises the core of the index semantics
 * (camel/core/index_semantics.h) without any module: the quasi-affine expression pool (its
 * simplifications, interning, text, evaluation and the forms it rejects), the structural
 * validation of instances, the query through the operator table for an operator of the
 * protocol `semtest` declared here, the text form, splitting dims for tiles and the strided
 * view of a row-major operand. It prints one line per check, `INDEX <check> <result>`, and
 * returns the program's graph unchanged. It can only be named when the environment variable
 * they are named by a test pipeline (fault.h).
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kIndexSemanticsTestPass = "test::index_semantics";

class IndexSemanticsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
