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
 * Created: Sep. 09, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <span>
#include <string_view>

#include "camel/execute/executor.h"

class BasicBuiltinExecutor : public Executor {
  public:
    BasicBuiltinExecutor(camel::core::context::context_ptr_t ctx);
    virtual ~BasicBuiltinExecutor() = default;

    static executor_ptr_t create(camel::core::context::context_ptr_t ctx);
};

/// Registers the builtin operators' traits (purity) with the core OperatorTable, then their
/// semantics (declareBuiltinOperatorSemantics).
void registerBuiltinOperatorTraits();

/// Declares the semantics of the built-in scalar arithmetic (builtin_semantics.cpp): stable
/// keys, numeric semantics and faults. Called after the traits, which it is checked against.
void declareBuiltinOperatorSemantics();

/// Declares the resources of the built-in operators that touch any (builtin_semantics.cpp;
/// docs/design/op-semantics section 8): `:var/*` and the assignments (`names`: the built-in
/// operator names, `op/assn...` among them) their variable cell, the console and the profiler
/// their ambient state. Called after the traits.
/// Whether the built-in operator `name` (without the leading ':') stores into a variable cell
/// (the assignments). The one place that names the family: purity and the resource
/// declarations both read it, nothing else infers effects from a name.
bool storesIntoCell(std::string_view name);

void declareBuiltinOperatorResources(std::span<const std::string_view> names);
