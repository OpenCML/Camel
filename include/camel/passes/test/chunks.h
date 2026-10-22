// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only check of the node chunks a commit shares between versions of a graph. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kChunksTestPass = "test::chunks";
class ChunksTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
