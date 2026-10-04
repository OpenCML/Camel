/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 10, 2026
 * Updated: Oct. 02, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Runtime rewrite pass configuration.
 *
 * `std::inline` is intentionally an inline-only surface. `std::opt` is the
 * aggregate rewrite pipeline that may also enable devirtualization and
 * specialization before inlining. Keeping the two configuration types distinct
 * prevents pass registration from accidentally collapsing back into the old
 * "inline means all optimizations" behavior.
 */

#pragma once

#include <cstddef>

/// Nothing is inlined into a graph that would then hold more nodes than this. A runtime graph
/// addresses its nodes, node blocks and edges with 16 bits; the cap keeps a graph the rewrite
/// grows well inside that representation.
inline constexpr size_t kMaxInlineOwnerNodes = 4096;

enum class InlineTargetStrategy {
    Small,
    Arm,
    Hybrid,
};

struct InlineRewriteConfig {
    InlineTargetStrategy inlineStrategy     = InlineTargetStrategy::Hybrid;
    size_t smallSubgraphMaxNonDataPortNodes = 8;
    /// An acyclic callee called from several places is inlined everywhere when the code this
    /// duplicates, (call sites - 1) x its size in non-data nodes, stays within this budget.
    size_t duplicationBudgetNodes = 256;
    /// Nothing is inlined into a graph that would then hold more nodes than this (see
    /// kMaxInlineOwnerNodes).
    size_t maxOwnerNodes             = kMaxInlineOwnerNodes;
    bool blockCallsToSccEntryCallees = true;
};

/// Nodes that specialization and inlining may add to a program (see
/// OptimizeRewriteConfig::growthBudgetNodes). The environment variable CAMEL_OPT_GROWTH_BUDGET
/// overrides it for every pass built on the rewrite engine.
inline constexpr size_t kDefaultGrowthBudgetNodes = 4096;
inline constexpr const char *kGrowthBudgetEnvVar  = "CAMEL_OPT_GROWTH_BUDGET";

struct OptimizeRewriteConfig {
    bool enableDevirtualization = true;
    bool enableSpecialization   = true;
    bool enableInlining         = true;
    /// Bound on growth. Specializing a call copies its callee and inlining copies it into the
    /// caller; with constant arguments the two unroll a recursion one level per round
    /// (`fib(15)` becomes its whole call tree). Once the pass has copied this many nodes,
    /// nothing further is specialized or inlined and the remaining calls stay calls: the
    /// program means the same, it is only less unrolled. Copies that folding removes again
    /// still count, so the bound is also a bound on the work of the pass.
    size_t growthBudgetNodes = kDefaultGrowthBudgetNodes;
    /// The nodes already copied, for a driver that runs the engine several times against one
    /// budget (std::opt::simplify). Null: each run of the engine has the whole budget.
    size_t *growthSpentNodes = nullptr;
    /// Splits an indirect call whose callee is projected out of a branch JOIN into one call
    /// per arm plus one JOIN per projected value. The result deliberately violates the
    /// scheduler invariant that a JOIN is the single cut between its BRCH and everything
    /// downstream (several sibling JOINs read the same BRCH and their users merge), so it is
    /// only valid for consumers that never build an execution topo — the ONNX exporter.
    bool enableBranchedCallSplit = false;
    InlineRewriteConfig inlineConfig{};
};
