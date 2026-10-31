// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The forward-mode (jvp) transform of a function graph: `reinterp<jvp>(f)`.
 *
 * `reinterp` produces a *new function*, not a rewritten graph. The author's ruling (a824031aa) is
 * that a published program's graph is never replaced -- that road was measured and ends in SIGSEGV
 * at the entry it makes (9cb1c3a94) -- so the transform builds a graph of its own, node by node,
 * and hands it to the call site as a function value. That is the same shape as
 * `camel::autodiff::buildGradientGraph` for the reverse direction, and it is why this lives beside
 * a value-level transform rather than in the pass pipeline: a `RuntimeGraphRewritePass` must answer
 * with a program whose root is the graph it was given (src/execute/camel_pass.cpp), which a new
 * function cannot be.
 *
 * The signature is `reinterp<jvp>(f) : (..Lift<ParamsOf<F>>) => Lift<ResultOf<F>>`: every parameter
 * becomes a pair of the value and its tangent, and so does the result. The tangent of an input is
 * *passed in* by the caller -- `jvp(f)(2.0, 1.0)` asks for the derivative in the direction 1 -- not
 * found inside the function. In the new graph each primitive's tangent comes from *its rule*: the
 * transform asks the ruleset (`stdlib/rulesets/jvp.cml`) for the rule filed under that operator and
 * operand count, instantiates it at the primitive's types, and calls it. The right side as an
 * instance is `(x, w) => ((dx, dw) => Tangent<T>)`, so it is called once with the operands and what
 * it answers is called with the tangents. A constant's tangent is zero and a port's is the tangent
 * the caller passed; the result's tangent is what the chain of rules computed. There is no table of
 * laws here: a primitive with no rule in the ruleset is refused by name -- `skip=rule uri=...` and
 * why -- rather than answered with 0 or 1, and the reading says where the rules came from
 * (`source=ruleset`).
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/source/ids.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph.h"

#include <string>

namespace camel::transforms {

/// The graph of `reinterp<jvp>(f)` and the program of the graphs built with it.
struct JvpGraph {
    const camel::runtime::Program *program = nullptr;
    camel::runtime::GCGraph *graph         = nullptr;
    /// Where the build stopped, when `graph` is null: the primitive the ruleset has no rule for, as
    /// the origin of that node in `function` (its source position), what the primitive is and why it
    /// was refused. A refusal that only prints is a reading, not a diagnosis: the kernel raises a
    /// fault *at this node* so the error names the operator in `f`, not the call that asked for `jvp`.
    camel::source::origin_id_t refusedOrigin = camel::source::kInvalidOriginId;
    std::string refusedUri;
    std::string refusedWhy;
    /// What could not be reinterpreted, when it is not a primitive (a loop): the name the diagnostic
    /// uses. A construct this transform has no reading for is refused the same way a primitive with
    /// no rule is -- at the construct, by name, stopping the build -- never copied and hoped for.
    std::string refusedConstruct;
};

/// The signature `reinterp<jvp>(f)` has for `function`, or nullptr when the function has no forward
/// transform (its result is not a float, or it takes no `float` parameter).
camel::core::type::FunctionType *jvpSignature(camel::core::type::FunctionType *function);

/// Builds the graph of `reinterp<jvp>(f)` for the graph `function` of `program`. The graph `function`
/// is read, never changed; the new graph is a member of a program of its own, the way
/// `buildGradientGraph` builds the reverse direction. `os` receives what was built: a graph the
/// caller cannot name yet is a graph nobody can check, so the reading is part of the interface.
JvpGraph buildJvpGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os);

} // namespace camel::transforms