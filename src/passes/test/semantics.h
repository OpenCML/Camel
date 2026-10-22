// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::operator_semantics` exercises the registration and query interface of
 * operator semantics (camel/core/operator_semantics.h, OperatorTable::declareSemantics) and
 * prints one line per check, `SEMANTICS <check> <result>`. It can only be named when the
 * environment variable they are named by a test pipeline (fault.h).
 *
 * It declares operators of the protocol `semtest`, which no module has: every registration
 * check of design section 4.4 that phase 1 implements gets one declaration that must be
 * rejected; then the version rules of bind, the declaration epoch, the key text and the
 * permission names. The program's graph is returned unchanged.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kOperatorSemanticsTestPass = "test::operator_semantics";

class OperatorSemanticsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
