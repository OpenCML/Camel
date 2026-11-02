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
 * The residuals are a property of the *body*, not of the type: `f(x) = x` has none and
 * `f(x) = x*x` has one, and the two have the same lifted type. So the tuple is read off the graph
 * (`residualsOf`) before either half is built, and the halves are typed with it -- the identity
 * graph's forward half answers `(result, ())`, `f(x, y) = x*x*y`'s answers a three-member tuple.
 * The residual position in `Split<F>` is therefore a hole the compiler fills where the graph is in
 * hand (author, 2026-11-01); the operator resolver, which sees types only, gets a stand-in -- one
 * member per value parameter -- and that stand-in is the type a call site is checked against, never
 * what the graphs carry. It is not a reading: nothing prints it and it is never compared with the
 * graph. The count a reading states is the graph's (`residuals=`), and the halves' types are the
 * graphs' (`type=`). Nothing on the far side of the cut is recomputed.
 *
 * Written up with the readings in
 * docs/handover/reviews/questions-2026-11-01-the-residual-slot-has-no-type-until-execution.md.
 *
 * `g(x) = x*x` at 3 reads `forward y= 9.0`, `residual r0= (3.0)` and `tangent dx1= 6.0`, and 12.0 and
 * 0.0 for dx = 2 and dx = 0. On a lifted graph that computes nothing (two parameter ports and one tuple
 * fill -- what `reinterp<jvp>` produces for `f(x) = x`) no value crosses at all: `fwd(3.0)` is 3.0,
 * `lin(r0, 1.0)` is 1.0, and the residual tuple is empty.
 *
 * What is built is the straight-line tier: the body is copied node by node -- a port, a constant, an
 * operator, a fill, and a call (a rule's right side reaches the transform as a call whose callee is a
 * graph of the program, so the call is copied with its inputs placed on that half's side) -- and
 * everything else is refused *by name* at the node it was written at: a function value built by the
 * body, a branch, a loop, and an effect edge. A value the backward half reads crosses the cut as a
 * residual whether or not it is a parameter, so nothing is recomputed on that side: what it reads is
 * in the tuple, at the place `residualsOf` gave it.
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

/// The halves when the residual tuple is known -- which it is as soon as the *graph* is in hand,
/// because which values cross the cut is a property of the body (`residualsOf`). `split` builds its
/// halves with this one, so the identity graph's forward half answers `(result, ())` and
/// `f(x, y) = x*x*y`'s answers a three-member tuple. `splitHalvesOf(function)` is the same call with
/// the placeholder a caller that has only types can know: one slot per value parameter, the shape the
/// operator resolver checks the call site against.
SplitHalves splitHalvesWith(
    camel::core::type::FunctionType *function, camel::core::type::Type *residuals);

/// One residual: a value the forward half computes and the backward half reads, so that the cut
/// between the two graphs carries it instead of recomputing it.
struct Residual {
    /// The node of the lifted graph that computes the value -- a parameter port, or a node the
    /// forward half keeps.
    camel::runtime::gc_node_ref_t node = camel::runtime::kInvalidNodeRef;
    camel::core::type::Type *type      = nullptr;
    /// The parameter it is the value of, when it is a parameter; `kNotAParameter` when it is a value
    /// the forward half computes.
    size_t parameter = 0;
    static constexpr size_t kNotAParameter = static_cast<size_t>(-1);
};

/// What crosses the cut, read from the graph: the answer the author's ruling (2026-11-01) asks for
/// where the graph is in hand, since it cannot be asked of the type. A residual is a value the
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
std::vector<Residual> residualsOf(
    const camel::runtime::GCGraph &lifted, const camel::runtime::Program *program = nullptr);

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
    /// How many values cross the cut (`residualsOf`, read from the graph): zero on the identity graph,
    /// one for `x*x`, and three for `f(x, y) = x*x*y` -- `x`, `y`, and `x*x`, because the backward
    /// half's rule for the second `mul` reads that product.
    size_t residuals = 0;
};

/// Builds the two graphs of `split(f)` and prints what it built. `f`'s graph is read, never changed.
SplitGraph buildSplitGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os);

/// Registers the cut's compile-time half: `split(f)` where `f` is a value the front end built while
/// the caller's graph was being built (split_instantiate.cpp). The shapes it does not answer keep
/// the kernel, which reads the same graph in the program it runs in.
void registerSplitCompileTimeTransform();

} // namespace camel::transforms