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
 * Updated: May. 02, 2026
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
    bool blockCallsToSccEntryCallees        = true;
};

struct OptimizeRewriteConfig {
    bool enableDevirtualization = true;
    bool enableSpecialization   = true;
    bool enableInlining         = true;
    InlineRewriteConfig inlineConfig{};
};
