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
 * Updated: Dec. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "bytecode.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "optimize.h"

struct CompileStrategy {
    bool enableTailCallDetection = true;
    bool enableInlineOperators   = false;

    // optimization strategies to apply
    OptimizationStrategyCode optimizationStrategies = OptimizationStrategyCode::None;
};

struct BytecodeIndex {
    size_t offset;
    size_t length;
    GraphIR::Graph *graph;
};

bytecode_vec_t
compile(const context_ptr_t &ctx, GraphIR::Graph *graph, const CompileStrategy &opt = {});

std::tuple<bytecode_vec_t, std::vector<BytecodeIndex>, std::unordered_map<GraphIR::Graph *, size_t>>
compileAndLink(context_ptr_t ctx, GraphIR::Graph *entry, const CompileStrategy &opt);

std::string opCodeToString(const Bytecode &bc, size_t index, const context_ptr_t &context);
