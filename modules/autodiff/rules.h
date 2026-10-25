// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Custom derivative rules for user functions (design principles 6.2, 8).
 *
 *   #vjp(rule)
 *   func f(x1, ..., xn): y { ... }
 *
 * `#vjp(rule)` is a declaration tag (`export tag vjp(rule: func) on func`): its value is the
 * graph reference of `rule`. The GIR builder keeps that graph in the reference table of f's
 * graph (kGraphRefRule), so the frozen graph names its rule directly: no rewritten copy of f,
 * no closure capture, no side table keyed by graph identity. When f is differentiated, calls of
 * f use the rule instead of differentiating f's body. The rule returns the gradient of f's
 * differentiable parameter, or a tuple of the gradients of its differentiable parameters (in
 * order) when it has several.
 *
 * A rule is a function of the program, not a closure: the tag carries a graph reference, so a
 * rule that captures values of the surrounding scope cannot be attached this way.
 */

#pragma once

#include "camel/core/rtdata/func.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

namespace camel::autodiff {

/// The rule of the function graph `graph`, nullptr when its declaration has no `#vjp(rule)`.
/// `program` holds both the function graph and the rule graph.
::Function *ruleOf(const camel::runtime::Program *program, const camel::runtime::GCGraph *graph);

/// The rule of the function value `function`, nullptr when it has none.
::Function *ruleOf(const ::Function *function);

/// Whether the function graph `graph` has a rule (`#vjp(rule)` on its declaration).
bool hasRule(const camel::runtime::GCGraph *graph);

} // namespace camel::autodiff
