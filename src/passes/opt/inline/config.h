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
    size_t duplicationBudgetNodes    = 256;
    bool blockCallsToSccEntryCallees = true;
};

struct OptimizeRewriteConfig {
    bool enableDevirtualization = true;
    bool enableSpecialization   = true;
    bool enableInlining         = true;
    /// Splits an indirect call whose callee is projected out of a branch JOIN into one call
    /// per arm plus one JOIN per projected value. The result deliberately violates the
    /// scheduler invariant that a JOIN is the single cut between its BRCH and everything
    /// downstream (several sibling JOINs read the same BRCH and their users merge), so it is
    /// only valid for consumers that never build an execution topo — the ONNX exporter.
    bool enableBranchedCallSplit = false;
    InlineRewriteConfig inlineConfig{};
};
