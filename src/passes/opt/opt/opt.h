// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "../inline/config.h"
#include "camel/execute/pass/opt.h"

class OptimizeRewritePass : public RuntimeGraphRewritePass {
  public:
    OptimizeRewritePass(
        const camel::core::context::context_ptr_t &ctx, const OptimizeRewriteConfig &config = {})
        : RuntimeGraphRewritePass(ctx), config_(config) {}
    ~OptimizeRewritePass() override = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    OptimizeRewriteConfig config_{};
};

/// The rules library's saturation under the core's name (`std::opt::saturate`, H225): the
/// standard rules of stdlib/rules.cml applied both ways to the writings of every value, for at
/// most `rounds` rounds, with no extraction. The program it hands on has values with several
/// writings; the whole point of the pass is to stop there, so the dumps that follow can draw the
/// classes (`std::gir`, `std::rgir`, `std::jgir`). `std::opt::extract` (or the driver, before a
/// pass that runs or translates the program without accepting that state) brings it back.
class OptimizeSaturatePass : public GraphIRPass {
  public:
    OptimizeSaturatePass(
        const camel::core::context::context_ptr_t &ctx, int64_t rounds, bool strict)
        : GraphIRPass(ctx), rounds_(rounds < 0 ? 0 : rounds), strict_(strict) {}
    ~OptimizeSaturatePass() override = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

    /// A program with several writings is what this pass produces and what it takes: saturating
    /// twice saturates the classes again.
    bool acceptsWritings() const override { return true; }

  private:
    int64_t rounds_ = 0;
    bool strict_    = false;
};
