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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Shared runtime optimization pass configuration.
 *
 * The runtime-native rewrite stack is being split phase by phase. `std::inline`
 * already runs entirely on GCGraph/GraphDraft. The devirtualization and
 * specialization flags remain here because the long-term pass surface will stay
 * phase-addressable, but those phases are not yet implemented in the new
 * runtime-native engine.
 */

#pragma once

#include <cstddef>

enum class InlineTargetStrategy {
    Small,
    Arm,
    Hybrid,
};

struct OptimizeRewriteConfig {
    bool enableDevirtualization             = true;
    bool enableSpecialization               = true;
    bool enableInlining                     = true;
    InlineTargetStrategy inlineStrategy     = InlineTargetStrategy::Hybrid;
    size_t smallSubgraphMaxNonDataPortNodes = 8;
    bool blockCallsToSccEntryCallees        = true;
};

using InlineRewriteConfig = OptimizeRewriteConfig;
