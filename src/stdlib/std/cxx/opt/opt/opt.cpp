// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "opt.h"

#include "camel/execute/pass/base.h"
#include "camel/execute/pass/registration.h"
#include "camel/passes/opt/generic.h"
#include "src/stdlib/std/cxx/opt/budget.h"

#include <format>

const camel::runtime::Program *
OptimizeRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    // A run starts from zero: what an earlier run of this instance counted was about a program this
    // one does not have (the count of not-settling runs is per run, and this is where a run starts).
    settleStreak_ = SettleStreak{};
    return simplifyGraph(context_, program, config_, os, 16, this);
}

const camel::runtime::Program *
OptimizeSaturatePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    // The laws live once, in stdlib/rules.cml (as they do for std::cxx::algebra): this pass is
    // `opt::saturate`, the library's saturation without its extraction, so a caller sees the state with several
    // writings. The two parameters are the library pass's own (design principles 9.3).
    const PassRunResult result = applyRegisteredPass(
        program,
        std::format(
            "opt::saturate rounds={} strict={}", rounds_, strict_ ? "true" : "false"),
        context_,
        os,
        this);
    return result.program;
}

namespace {

using camel::execute::constantBudgetOf;
using camel::execute::growthBudgetOf;
using camel::execute::PassKernel;
using camel::execute::settleRunsOf;

/// `settle_runs` is how many settle rounds the engine may take before it reports where values
/// still have several writings. The declaration's type is `int` and leaves 0 writable; 0 is not a
/// number of rounds, so it is refused here, by the pass that reads it, and not by a table of pass
/// names in the pipeline (design principles 9: a pass is a function; what its parameters mean is
/// its own business).
std::optional<ParamRefusal> settleRunsRefusal(const PassArgs &args) {
    if (!args.given("settle_runs")) {
        return std::nullopt;
    }
    const int64_t rounds = args.getInt("settle_runs");
    if (rounds >= 1) {
        return std::nullopt;
    }
    return ParamRefusal{
        .name     = "settle_runs",
        .text     = args.text("settle_runs"),
        .expected = "at least 1",
        .reason = std::format("settle_runs must be at least 1, got {}", args.text("settle_runs"))};
}

/// `std::opt`, the C++ implementation the default pipeline's name `std::opt` selects.
const PassKernel optimizeKernel(
    "std::opt",
    [](const camel::core::context::context_ptr_t &ctx, const PassArgs &args) {
        OptimizeRewriteConfig config;
        config.safeFloatingPoint   = args.getBool("safe_fp");
        config.checkScreens        = args.getBool("check_screens");
        config.growthBudget        = growthBudgetOf(args, "std::opt");
        config.constantBudgetBytes = constantBudgetOf(args);
        config.settleRuns          = settleRunsOf(args);
        return std::make_unique<OptimizeRewritePass>(ctx, config);
    },
    settleRunsRefusal, true);

/// The saturation of the rules library, under the core's name. Like `std::cxx::algebra`, the laws
/// are the library's (`opt::saturate`); unlike it, the extraction is not made, so the program stops
/// in the state where a value has several writings.
const PassKernel saturateKernel(
    "std:cxx::saturate",
    [](const camel::core::context::context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<OptimizeSaturatePass>(
            ctx,
            args.getInt("rounds"),
            args.getBool("strict"));
    },
    true);

} // namespace
