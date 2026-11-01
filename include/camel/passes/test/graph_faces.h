// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only reading of the facts the read face answers beside a node's record (see graph_faces.cpp).
 */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kGraphFacesTestPass = "test::graph_faces";
class GraphFacesTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};