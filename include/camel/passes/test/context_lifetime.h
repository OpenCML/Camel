// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only reading of a `Context`'s lifetime: a second context built beside the running one is
 * destroyed when its own scope ends, gives the allocator registration it installed back, and
 * drains the deferred releases its teardown queued. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kContextLifetimeTestPass = "test::context_lifetime";
class ContextLifetimeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};