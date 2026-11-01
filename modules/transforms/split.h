// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The split of a lifted function: `split(f)`, the step between `jvp` and `transpose`
 * (`vjp(f) = transpose(split(reinterp<jvp>(f)))`, design 4.4).
 *
 * A lifted function carries a value and a tangent for every parameter, and answers with the pair
 * (result, tangent). `split` cuts that function in two along the pair, not along a position:
 *
 *   - the **forward half** takes the value of every parameter, keeps the part of the body that
 *     computes on values, and answers with the result and the **residuals** -- the forward values
 *     the backward half's rules multiply by, handed on beside the result;
 *   - the **backward half** takes the residuals and the tangent of every parameter, keeps the part of
 *     the body that reads a tangent, and answers with the tangent of the result. A forward value it
 *     reads is projected out of the residual tuple (ACCS, TupleIndex), so the cut is a value crossing
 *     between two graphs and not a recomputation.
 *
 * The residuals are a tuple in the signature (`ResidualsOf<F>`, the compiler's type function, not
 * written yet): one member per value parameter, in program order. The graph says which values cross
 * the cut, and `residualsOf` reads that from the graph; the signature says how many the tuple may
 * hold. Where the two differ the difference is measured and printed (`residuals=`, `carried=`,
 * `slots=`), never quietly assumed away.
 *
 * `g(x) = x*x` at 3 reads `forward y= 9.0`, `residual r0= (3.0)` and `tangent dx1= 6.0`, and 12.0 and
 * 0.0 for dx = 2 and dx = 0. On a lifted graph that computes nothing (two parameter ports and one tuple
 * fill -- what `reinterp<jvp>` produces for `f(x) = x`) no value crosses at all: `fwd(3.0)` is 3.0,
 * `lin(r0, 1.0)` is 1.0, and the signature's one slot is a value parameter nothing reads.
 *
 * What is built is the straight-line tier: the body is copied node by node -- a port, a constant, an
 * operator, a fill, and a call (a rule's right side reaches the transform as a call whose callee is a
 * graph of the program, so the call is copied with its inputs placed on that half's side) -- and
 * everything else is refused *by name* at the node it was written at: a function value built by the
 * body, a branch, a loop, and an effect edge. A value that is not a parameter and that the backward
 * half needs is *recomputed* there this tier, because the tuple has no slot to carry it: that is a
 * correct graph, but it is a residual the cut did not carry, and `carried=` counts the ones it did.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/source/ids.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/graph.h"

#include <iosfwd>
#include <string>
#include <vector>

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

/// One residual: a value the forward half computes and the backward half reads, so that the cut
/// between the two graphs carries it instead of recomputing it.
struct Residual {
    /// The node of the lifted graph that computes the value -- a parameter port, or a node the
    /// forward half keeps.
    camel::runtime::gc_node_ref_t node = camel::runtime::kInvalidNodeRef;
    camel::core::type::Type *type      = nullptr;
    /// The parameter it is the value of, when it is a parameter; `kNotAParameter` when it is a value
    /// the forward half computes (the next tier: this one refuses to carry those).
    size_t parameter = 0;
    static constexpr size_t kNotAParameter = static_cast<size_t>(-1);
};

/// `ResidualsOf<F>` -- the type function the author's ruling hands to the compiler (2026-10-31,
/// pages 105-110), implemented where it can be computed: from the *graph*. A residual is a value the
/// forward half computes and the backward half reads, so the answer is a property of the body and
/// not of the signature: `f(x) = x` has none and `f(x) = x*x` has one, and the two functions have
/// the same lifted type.
///
/// The rule, in the graph: start at the exit's two components (the result and the tangent); a node
/// the result reaches is computed before the cut, and a node the tangent reaches is computed after
/// it. Where a walk from the tangent side arrives at a node that is already on the result side, that
/// node's value crosses the cut -- it is a residual, and the walk stops there rather than descending
/// into the forward half.
///
/// Program order: parameters by their index, then computed values by their node. The answer is empty
/// when the graph is not a lifted graph (the exit is not the pair), which is when there is nothing
/// to cut.
std::vector<Residual> residualsOf(const camel::runtime::GCGraph &lifted);

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
    /// How many slots the residual tuple has, read from the backward half's signature. Part of the
    /// reading: the type says how many values the tuple *may* hold.
    size_t residualSlots = 0;
    /// How many values actually cross the cut (`residualsOf`, read from the graph): zero on the
    /// identity graph, one for `x*x`, and three for `f(x, y) = x*x*y` -- `x`, `y`, and `x*x`, because
    /// the backward half's rule for the second `mul` reads that product.
    size_t residuals = 0;
    /// How many of those the tuple can carry: the ones that are parameter values. The rest the
    /// backward half recomputes this tier, and the signature cannot name a slot for them.
    size_t carried = 0;
};

/// Builds the two graphs of `split(f)` and prints what it built. `f`'s graph is read, never changed.
SplitGraph buildSplitGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os);

} // namespace camel::transforms