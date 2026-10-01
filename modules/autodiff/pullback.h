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
 * Reverse-mode differentiation of runtime graphs by pullback closures.
 *
 * After macros have run, a program is a static graph, so derivatives are a
 * graph rewrite rather than a runtime tape. For a function graph g the
 * transform produces
 *
 *     g.fwd : (inputs) => (value, pullback)
 *     g.pb  : (dy) => tangents of g's inputs        (a closure over residuals)
 *
 * g.fwd is g with every differentiable call rewritten to call the callee's
 * .fwd graph; g.pb walks g backward from dy, applying the rules published in
 * the core DerivativeRegistry to operators and calling the pullbacks returned
 * by callees. The captured residuals and callee pullbacks are the tape, held
 * by ordinary closures.
 *
 * Control flow needs no special runtime support:
 *   - a branch's arms are subgraph calls; each arm is differentiated like a
 *     callee (after giving all arms the same inputs), and the JOIN merges
 *     their (value, pullback) pairs, so the pullback of the taken arm is the
 *     one called backward;
 *   - recursion is a call of a graph whose .fwd is being built: the pullback
 *     of one step captures the pullback of the next.
 * When shapes are static, inlining and simplification passes flatten the
 * closures back into straight-line code.
 *
 * grad(f) itself is built the same way, except that f's backward pass is
 * emitted into f's own forward graph instead of a separate pullback.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph.h"

#include <optional>
#include <vector>

namespace camel::autodiff {

namespace type = camel::core::type;

/// What grad(f) differentiates and returns. Gradients are taken with respect to f's
/// differentiable with-parameters (the model) when it has any, otherwise with respect to its
/// differentiable norm-parameters. One parameter gives its tangent, several a tuple of tangents.
struct GradientSignature {
    std::vector<size_t> withPorts;
    std::vector<size_t> normPorts;
    type::Type *gradientType = nullptr;
    /// Type of grad(f), or of value_and_grad(f) whose result is (value, gradient).
    type::FunctionType *functionType = nullptr;
};

/// The signature of grad(f) (or value_and_grad(f)), or nullopt when f cannot be differentiated:
/// its result is not a float, or none of its parameters has a tangent.
std::optional<GradientSignature> gradientSignature(type::FunctionType *function, bool withValue);

/// Builds the graph of grad(f) (or value_and_grad(f)) for the graph of f.
camel::runtime::GCGraph *buildGradientGraph(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *function,
    bool withValue);

} // namespace camel::autodiff
