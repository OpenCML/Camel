// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only pass that throws the exception a library container throws when `operator new` cannot
 * satisfy it: a bare `std::bad_alloc`, not the runtime's own `mm::MemoryExhausted` (a derived
 * class). It is the probe for the rule that both kinds of allocation failure are reported the same
 * way, as RuntimeDiag::MemoryExhausted, instead of the first one being read as a compiler defect.
 */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kThrowBadAllocTestPass = "test::throw_bad_alloc";
class ThrowBadAllocTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};