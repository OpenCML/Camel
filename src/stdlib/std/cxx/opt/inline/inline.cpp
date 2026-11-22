// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "inline.h"

#include "engine.h"

#include "camel/execute/pass/registration.h"
#include "src/stdlib/std/cxx/opt/budget.h"

#include <optional>

const camel::runtime::Program *
InlineRewritePass::apply(const camel::runtime::Program *program, std::ostream &) {
    OptimizeRewriteConfig config{
        .enableDevirtualization = false,
        .enableSpecialization   = false,
        .enableInlining         = true,
        .growthBudget           = growthBudget_,
        .inlineConfig           = config_,
    };
    // The engine counts the runs that changed one graph; the count is this pass's, so it lives as
    // long as the pass and no longer (engine.cpp requires it).
    config.settleStreak = &settleStreak_;
    // Its growth budget is this pass's too: `stdlib/std.cml` declares `growth_budget` on the pass,
    // so the count a run spends is one the pass keeps, cleared here where the run starts.
    growthSpent_            = 0;
    config.growthSpentNodes = &growthSpent_;
    return applyOptimizeRewritePass(context_, program, config);
}

namespace {

using camel::execute::growthBudgetOf;
using camel::execute::PassKernel;

/// The four inline strategies the core names: `std::cxx::inline` (the hybrid one, no strategy of
/// its own) and the three explicit ones.
PassMaker inlineKernelFor(std::optional<InlineTargetStrategy> strategy, std::string passName) {
    return
        [strategy, passName = std::move(passName)](
            const camel::core::context::context_ptr_t &ctx,
            const PassArgs &args) -> std::unique_ptr<GraphIRPass> {
            InlineRewriteConfig config{};
            if (strategy) {
                config.inlineStrategy = *strategy;
            }
            return std::make_unique<InlineRewritePass>(ctx, config, growthBudgetOf(args, passName));
        };
}

const PassKernel inlineKernel("std::cxx::inline", inlineKernelFor(std::nullopt, "std::cxx::inline"));
const PassKernel smallInlineKernel(
    "std::cxx::small",
    inlineKernelFor(InlineTargetStrategy::Small, "std::cxx::small"));
const PassKernel armInlineKernel(
    "std::cxx::arm",
    inlineKernelFor(InlineTargetStrategy::Arm, "std::cxx::arm"));
const PassKernel hybridInlineKernel(
    "std::cxx::hybrid",
    inlineKernelFor(InlineTargetStrategy::Hybrid, "std::cxx::hybrid"));

} // namespace
