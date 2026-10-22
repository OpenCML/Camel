// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only check that a graph a session encoded and no program binds is freed by the collector. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kGraphReleaseTestPass = "test::graph_release";
class GraphReleaseTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};