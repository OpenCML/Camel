// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The split of a lifted function: `split(f)`, the step between `jvp` and `transpose`
 * (`vjp(f) = transpose(split(reinterp<jvp>(f)))`, design 4.4).
 *
 * A lifted function carries a value and a tangent for every parameter, and answers with the pair
 * (result, tangent). `split` cuts that function in two along the pair, not along a position:
 *
 *   - the **forward half** takes the value of every parameter and answers with the result and the
 *     residuals -- the values the backward half will need, which today is the empty tuple;
 *   - the **backward half** takes the residuals and the tangent of every parameter and answers with
 *     the tangent of the result.
 *
 * On a lifted graph that computes nothing (two parameter ports and one tuple fill -- what
 * `reinterp<jvp>` produces for `f(x) = x`) the cut leaves the value port in the forward half, the
 * tangent port in the backward half, and **no** residuals: `fwd(3.0)` is 3.0, `lin(r0, 1.0)` is 1.0
 * and `r0` holds nothing. A residual that appears there would be a value moved into the residuals
 * that nothing reads.
 *
 * What is built today is the plumbing, on the tier that graph has: the body is copied node by node
 * (a port, a constant, an operator, a fill), the exit's two components are followed separately, and
 * everything else is refused *by name* at the node it was written at -- a call, a branch, a loop, an
 * effect edge, and a backward half that would have to read the value of a parameter (a residual the
 * two halves cannot carry yet, because a runtime graph cannot take a tuple parameter apart). The
 * refusal is the reason the readings of a `jvp` lift of `x*x` wait for the transform that rewrites
 * that body instead of calling the rule's function.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/source/ids.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/graph.h"

#include <iosfwd>
#include <string>

namespace camel::transforms {

/// The two graphs `split(f)` answers with, as types: `(forward, backward)`, the pair the call's
/// result is. `why` says why there are none when the function is outside what the step takes.
struct SplitHalves {
    camel::core::type::FunctionType *forward   = nullptr;
    camel::core::type::FunctionType *backward  = nullptr;
    const camel::core::type::TupleType *pair   = nullptr;
    std::string why;
};

/// The halves of a lifted function: every ordinary parameter is the pair `(value, tangent)` and the
/// exit is the pair `(result, tangent)`, so the counts are even in and two out. nullptr halves when
/// the function is not of that shape.
SplitHalves splitHalvesOf(camel::core::type::FunctionType *function);

/// The two graphs of `split(f)` and the program of them.
struct SplitGraph {
    const camel::runtime::Program *program = nullptr;
    camel::runtime::GCGraph *forward       = nullptr;
    camel::runtime::GCGraph *backward      = nullptr;
    /// Where the build stopped, when the graphs are null: the construct it could not copy, as the
    /// origin of that node in `function`.
    camel::source::origin_id_t refusedOrigin = camel::source::kInvalidOriginId;
    std::string refusedConstruct;
    std::string refusedUri;
    std::string refusedWhy;
    /// How many values the forward half hands on as residuals. Zero on the identity graph, and part
    /// of the reading: a value that is not needed there and is handed on anyway is a defect.
    size_t residuals = 0;
};

/// Builds the two graphs of `split(f)` and prints what it built. `f`'s graph is read, never changed.
SplitGraph buildSplitGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os);

} // namespace camel::transforms