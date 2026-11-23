// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: one graph, two readings of the same node's input ports (see view_ports.cpp). */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kViewPortsTestPass = "test::view_ports";
class ViewPortsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};