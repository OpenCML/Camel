// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only reading of the one question the four node kinds make two: whether an OPER node names an
 * operator (see operator_records.cpp and `kernel/graph/graph_view.h`, "the two-step reading").
 */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kOperatorRecordsTestPass = "test::operator_records";
class OperatorRecordsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};