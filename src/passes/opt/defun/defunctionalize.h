// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/opt.h"

/// std::defunctionalize: every function the program takes as a value becomes one variant of the enum
/// that stands for its type -- the values it captured are that variant's data -- and every call
/// through such a value becomes a direct call of the function it names, with the captures as
/// trailing arguments. See defunctionalize.cpp for what it requires and what it refuses.
///
/// `report` says whether the pass writes what it did and what it left: a program that names it is
/// asking for that answer, and the pipeline that runs it ahead of `std::opt` is not (it would put a
/// report line on the program's own output). `debug` writes one more level: the dispatch it is
/// about to lower, member by member and variant by variant, for reading a program it cannot lower.
class DefunctionalizeRewritePass : public RuntimeGraphRewritePass {
  public:
    DefunctionalizeRewritePass(
        const camel::core::context::context_ptr_t &context, bool report, bool debug = false)
        : RuntimeGraphRewritePass(context), report_(report), debug_(debug) {}
    ~DefunctionalizeRewritePass() override = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    bool report_ = true;
    bool debug_  = false;
};