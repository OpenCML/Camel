// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/opt.h"

class MacroRewritePass : public RuntimeGraphRewritePass {
  public:
    /// `report` writes where the pass is, one line per reachable graph per round, on stderr: the
    /// graph, its calls, the rewrites applied so far, its branches and its loops. A run that never
    /// comes back has then said which graph it was in before it stopped saying anything. Off
    /// unless the pass is named with `report=true` (stdlib/std.cml, `macro`), because a program
    /// that names the pass is asking for the expansion and not for a line on its stderr.
    MacroRewritePass(const camel::core::context::context_ptr_t &ctx, bool report = false)
        : RuntimeGraphRewritePass(ctx), report_(report) {};
    virtual ~MacroRewritePass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    bool report_ = false;
};