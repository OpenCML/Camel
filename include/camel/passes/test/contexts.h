// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only check that the process-level GC state of a `Context` is registered and given back by
 * owner: a second context beside a running one must not take over its root set or its graph
 * sweeper, and destroying the second must leave the first's registration in place. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kContextsTestPass = "test::contexts";
class ContextsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};