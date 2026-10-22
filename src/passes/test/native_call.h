// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Test-only native callbacks into original structured graphs; no module dependency.

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kNativeCallbackTestPass = "test::native_callback";
class NativeCallbackTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
