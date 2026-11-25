// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: a node's type has one answer, wherever the type sits (see view_types.cpp). */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kViewTypesTestPass = "test::view_types";
class ViewTypesTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};