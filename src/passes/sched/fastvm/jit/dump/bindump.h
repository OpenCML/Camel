// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/runtime_trans.h"

namespace ctx = camel::core::context;

/**
 * JitBinaryDumpPass: attempts to JIT-compile every graph and prints the
 * generated machine code in a visualized form.
 * The output follows LinkedBytecodeDumpPass formatting and prints each graph's
 * machine code in hexadecimal.
 */
class JitBinaryDumpPass : public RuntimeGraphTranslatePass {
  public:
    JitBinaryDumpPass(const ctx::context_ptr_t &ctx) : RuntimeGraphTranslatePass(ctx) {}
    virtual ~JitBinaryDumpPass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
