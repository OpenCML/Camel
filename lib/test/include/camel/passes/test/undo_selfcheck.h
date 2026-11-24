// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: the rollback self-check. Every class the journal records is written through the
 * setter that records it and put back by a rollback, and then the same round is run with one
 * class's hook made a no-op -- which is what a forgotten hook amounts to -- and the check has to
 * name that class. The program is passed on unchanged. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kUndoSelfCheckTestPass = "test::undo_selfcheck";
class UndoSelfCheckTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};