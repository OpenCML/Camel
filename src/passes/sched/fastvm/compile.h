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
 * Created: Oct. 21, 2025
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "bytecode.h"
#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"
#include "optimize.h"

namespace ctx = camel::core::context;

struct CompileStrategy {
    bool enableTailCallDetection = true;
    bool enableInlineOperators   = false;

    // optimization strategies to apply
    OptimizationStrategyCode optimizationStrategies = OptimizationStrategyCode::None;
};

struct BytecodeIndex {
    size_t offset;
    size_t length;
    camel::runtime::GCGraph *runtimeGraph;
};

struct LinkedBytecodeResult {
    bytecode_vec_t codes;
    std::vector<BytecodeIndex> graphs;
    std::unordered_map<camel::runtime::GCGraph *, size_t> offsetMap;
};

bytecode_vec_t compile(
    const ctx::context_ptr_t &ctx, camel::runtime::GCGraph *graph, const CompileStrategy &opt = {},
    std::unordered_map<size_t, camel::source::origin_id_t> *localPcOrigins = nullptr);

LinkedBytecodeResult
compileAndLink(ctx::context_ptr_t ctx, camel::runtime::GCGraph *entry, const CompileStrategy &opt);

std::string opCodeToString(const Bytecode &bc, const ctx::context_ptr_t &context);
