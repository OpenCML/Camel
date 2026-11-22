// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/runtime_trans.h"

namespace ctx = camel::core::context;

/**
 * JitRmirDumpPass (std::rmir): MIR translated directly from bytecode, based on
 * virtual registers and not yet optimized.
 */
class JitRmirDumpPass : public RuntimeGraphTranslatePass {
  public:
    JitRmirDumpPass(const ctx::context_ptr_t &ctx) : RuntimeGraphTranslatePass(ctx) {}
    virtual ~JitRmirDumpPass() = default;
    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/**
 * JitMirDumpPass (std::mir): final optimized vreg MIR after multiple passes
 * (optimization entry point: runMirOptimizationPasses).
 */
class JitMirDumpPass : public RuntimeGraphTranslatePass {
  public:
    JitMirDumpPass(const ctx::context_ptr_t &ctx) : RuntimeGraphTranslatePass(ctx) {}
    virtual ~JitMirDumpPass() = default;
    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
