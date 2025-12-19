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
 * Updated: Dec. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "bytecode.h"
#include "compile/gir.h"
#include "core/context/context.h"

#include <memory>

struct OptimizationStrategy {
    bool enableTailCallDetection = true;
    bool enableInlineOperators   = false;
};

bytecode_vec_t
precompile(const context_ptr_t &ctx, GraphIR::Graph *graph, const OptimizationStrategy &opt = {});

std::string opCodeToString(const Bytecode &bc, size_t index, const context_ptr_t &context);
