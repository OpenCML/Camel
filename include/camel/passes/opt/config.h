// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Runtime rewrite pass configuration.
 *
 * `std::opt::inline` is intentionally an inline-only surface. `std::opt` is the
 * integrated optimization driver, which combines call rewriting with folding, CSE
 * and DCE. This configuration selects the call-rewrite stages and their budgets.
 * Keeping the two configuration types distinct
 * prevents pass registration from accidentally collapsing back into the old
 * "inline means all optimizations" behavior.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// Nothing is inlined into a graph that would then hold more nodes than this: the cap keeps the
/// graphs the rewrite grows small enough to analyze.
inline constexpr size_t kMaxInlineOwnerNodes = 4096;

enum class InlineTargetStrategy {
    Small,
    Arm,
    Hybrid,
};

struct InlineRewriteConfig {
    InlineTargetStrategy inlineStrategy  = InlineTargetStrategy::Hybrid;
    /// How small a callee must be to be inlined as a small one, measured in the callee's own
    /// computation: its OPERs, the calls it makes and its structural nodes. A constant, a
    /// parameter, a GATE and the bookkeeping an inline splice adds are not counted, so the same
    /// function answers the same whatever the splice does (see engine.cpp,
    /// inlinedComputeNodeCount; "how small is a function" measures the
    /// compute nodes of the function being inlined).
    size_t smallSubgraphMaxComputeNodes = 8;
    /// An acyclic callee called from several places is inlined everywhere when the code this
    /// duplicates, (call sites - 1) x its size in non-data nodes, stays within this budget.
    size_t duplicationBudgetNodes = 256;
    /// Nothing is inlined into a graph that would then hold more nodes than this (see
    /// kMaxInlineOwnerNodes).
    size_t maxOwnerNodes             = kMaxInlineOwnerNodes;
    bool blockCallsToSccEntryCallees = true;
};

/// Nodes that specialization and inlining may add to a program (see GrowthBudget). It is the
/// default of the `growth_budget` parameter every pass built on the rewrite engine declares.
inline constexpr size_t kDefaultGrowthBudgetNodes = 4096;

/// The growth budget of one pass built on the rewrite engine. `nodes` is what the pass may copy;
/// `written` says the pipeline wrote the `growth_budget` parameter (as against the default), and
/// `passName` is the pass that owns it. A later pass that has to explain a call the engine left
/// (onnx::export) reads all three from InlineReport.
struct GrowthBudget {
    size_t nodes = kDefaultGrowthBudgetNodes;
    bool written = false;
    std::string passName;
};

/// Why the rewrite engine left a direct call as a call.
enum class InlineDecline : uint8_t {
    GrowthBudget,     // the copy does not fit what is left of the growth budget
    OwnerSize,        // the caller would exceed InlineRewriteConfig::maxOwnerNodes
    Recursive,        // the callee is recursive (it is not copied into its callers)
    ReachesRecursion, // the callee is not small and reaches a recursive function
    Duplication,      // the callee is not small and copying it to every call site costs too much
    InlinedCopy,      // the call lies in an inlined copy of the callee: the function would be
                      // copied into itself (the inline history; design 9.4)
    Strategy,         // the inline strategy does not select this call
};

/// One direct call the engine examined and did not inline, by the names of the two graphs.
struct InlineDeclined {
    std::string caller;
    std::string callee;
    InlineDecline why = InlineDecline::Strategy;
    /// The nodes inlining this callee would copy: what the growth budget and the owner cap pay
    /// for (see InlineRewriteConfig::maxOwnerNodes).
    size_t calleeNodes = 0;
    /// The callee's own computation (the measure of how small it is, see
    /// InlineRewriteConfig::smallSubgraphMaxComputeNodes): what a message about its size reports
    /// as "the inlined function has N compute nodes".
    size_t calleeComputeNodes = 0;
    size_t callerNodes        = 0;
    size_t callSites          = 0; // direct calls of the callee in the program
};

/// What the last run of the rewrite engine with inlining enabled decided about the calls it
/// left, recorded on the context as the pass fact kInlineReportFactKey (Context::setPassFact).
/// The graph a pass hands on does not show why a call in it is still a call; a pass that has to
/// refuse such a graph (onnx::export: a request it cannot see) reads the reason here.
struct InlineReport {
    size_t growthBudget      = 0;
    size_t growthSpent       = 0;
    bool budgetFromParameter = false; // the pipeline wrote growth_budget
    std::string passName;             // the pass that ran the engine (e.g. "std::opt")
    size_t maxOwnerNodes     = 0;
    size_t smallNodes        = 0; // InlineRewriteConfig::smallSubgraphMaxComputeNodes
    std::vector<InlineDeclined> declined; // the calls examined in the engine's last round

    const InlineDeclined *find(std::string_view caller, std::string_view callee) const {
        for (const InlineDeclined &entry : declined) {
            if (entry.caller == caller && entry.callee == callee) {
                return &entry;
            }
        }
        return nullptr;
    }
};
inline constexpr const char *kInlineReportFactKey = "std::opt/inline-report";

struct OptimizeRewriteConfig {
    bool enableDevirtualization = true;
    bool enableSpecialization   = true;
    bool enableInlining         = true;
    /// Bound on growth. Specializing a call copies its callee and inlining copies it into the
    /// caller; with constant arguments the two unroll a recursion one level per round
    /// (`fib(15)` becomes its whole call tree). Once the pass has copied this many nodes,
    /// nothing further is specialized or inlined and the remaining calls stay calls: the
    /// program means the same, it is only less unrolled. Copies that folding removes again
    /// still count, so the bound is also a bound on the work of the pass. The pass declares it
    /// as the `growth_budget` parameter (see GrowthBudget).
    GrowthBudget growthBudget{};
    /// The nodes already copied, for a driver that runs the engine several times against one
    /// budget (std::opt). Null: each run of the engine has the whole budget.
    size_t *growthSpentNodes = nullptr;
    InlineRewriteConfig inlineConfig{};
    /// Restrict algebra to strictly equivalent floating-point rewrites. By default local
    /// reassociation is allowed; precision changes and approximate functions are not implied.
    bool safeFloatingPoint = false;
    /// Hand the graphs the generic passes skip (a screen says the pass has nothing to do there,
    /// or the pass already left that content alone) to the pass anyway and fail when it rewrites
    /// one: a test of the screens (generic/rewrite.h), which must admit every graph a rewrite
    /// applies to.
    bool checkScreens = false;
};
