// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

/// What grad(f) differentiates and returns. The differential form differentiates f's **first
/// `with` parameter** (the model's weights, design principles 9.9); the other `with` parameters
/// are constants and the ordinary parameters are data. The gradient is shaped like that first
/// `with` parameter.
struct GradientSignature {
    type::Type *gradientType = nullptr;
    /// Type of grad(f), or of with_grad(f) whose result is (value, gradient).
    type::FunctionType *functionType = nullptr;
};

/// Why f cannot be differentiated: its result is not a float, it has no `with` parameter, its
/// first `with` parameter is a `var`, or that parameter's type has no tangent. Empty when it can
/// be differentiated.
std::string differentiationRefusal(type::FunctionType *function);

/// The signature of grad(f) (or with_grad(f)), or nullopt when f cannot be differentiated
/// (see differentiationRefusal).
std::optional<GradientSignature> gradientSignature(type::FunctionType *function, bool withValue);

/// The graph of a gradient and the program of the graphs built with it.
struct GradientGraph {
    const camel::runtime::Program *program = nullptr;
    camel::runtime::GCGraph *graph         = nullptr;
};

/// Builds the graph of grad(f) (or with_grad(f)) for the graph `function` of `program`.
GradientGraph buildGradientGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, bool withValue);

} // namespace camel::autodiff
