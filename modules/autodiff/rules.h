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
 * Author: Camel Contributors
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Custom derivative rules for user functions.
 *
 *   @vjp<rule>
 *   func f(x1, ..., xn): y { ... }
 *
 * attaches `rule(x1, ..., xn, dy)` to f: when differentiated, calls of f use
 * the rule instead of differentiating f's body. The rule returns the gradient
 * of f's differentiable parameter, or a tuple of the gradients of its
 * differentiable parameters (in order) when it has several.
 *
 * The decorator returns f with the rule attached as a captured value: a copy
 * of f's graph, marked as carrying a rule, closed over the rule function. The
 * association thus lives in the program itself and survives every graph
 * rewrite, instead of in a side table keyed by graph identity.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/graph.h"

namespace camel::autodiff {

/// f with `rule` attached.
::Function *attachRule(
    const camel::core::context::context_ptr_t &context, ::Function *function, ::Function *rule);

/// Whether a function graph was produced by attachRule.
bool carriesRule(const camel::runtime::GCGraph *graph);

/// The rule attached to a function value produced by attachRule.
::Function *ruleOf(const ::Function *function);

} // namespace camel::autodiff
