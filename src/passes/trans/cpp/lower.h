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
 * Created: Mar. 11, 2026
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/compile/gir.h"

#include <string>
#include <string_view>
#include <vector>

enum class CppEmitMode {
    Plain,
    Module,
    Inspect,
    Bench,
};

struct LoweringIssue {
    const GIR::Node *node;
    std::string reason;
};

struct GraphLoweringPlan {
    GIR::Graph *graph = nullptr;
    std::vector<GIR::Node *> topoNodes;
    bool frameElidable  = false;
    bool directCallable = false;
    std::vector<LoweringIssue> issues;
};

enum class CppOperatorEmitKind {
    InlineBinary,
    InlineUnary,
    NumericCast,
    RuntimeFallback,
};

struct CppOperatorSpec {
    std::string_view uri;
    CppOperatorEmitKind emitKind;
    int normArity;
    int withArity;
    std::string_view symbol;
};

GraphLoweringPlan analyzeGraphForCpp(GIR::Graph *graph);
const CppOperatorSpec *findCppOperatorSpec(const GIR::OperNode *node);
bool canRuntimeFallbackOperatorForCpp(const GIR::OperNode *node);
