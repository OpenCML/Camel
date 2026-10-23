// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// `std::cml`: print a runtime program as Camel source (handover H240, step S1).

#pragma once

#include "camel/execute/pass/params.h"
#include "camel/execute/pass/runtime_trans.h"
#include "camel/runtime/program.h"

#include <cstddef>

struct CmlOptions {
    /// The largest constant, in bytes of its storage, the printer writes as a literal. A larger
    /// one is refused and reported by name (the review's big-constant ruling).
    size_t maxLiteralBytes = 64 * 1024;
};

/// The implementation of `std::cml`; its name, documentation and parameters are the`r`n/// declaration in stdlib/std.cml.
PassMaker cmlPass();

class CmlDumpPass : public RuntimeGraphTranslatePass {
  public:
    explicit CmlDumpPass(
        const camel::core::context::context_ptr_t &ctx, CmlOptions options = {})
        : RuntimeGraphTranslatePass(ctx), options_(options) {}
    const camel::runtime::Program *apply(const camel::runtime::Program *, std::ostream &) override;
    /// The printer reads graphs as they are and writes no writings.
    bool acceptsWritings() const override { return false; }

  private:
    CmlOptions options_;
};
