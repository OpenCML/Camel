// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The transpose of a linear function's graph: `transpose(f)`.
 *
 * Where `jvp` builds a graph that runs the data flow forward with a tangent beside it, this builds
 * a graph that runs the data flow the other way: the caller passes the cotangent of the result, and
 * what comes out is the cotangent of the parameter. Nothing of `f`'s graph is changed -- the same
 * ruling (a824031aa) that makes `reinterp` produce a new function applies here, and for the same
 * reason: a published graph is never replaced.
 *
 * Straight-line code only, for now. Every primitive on the path must have one operand that carries
 * the data and only constants beside it, which is what `is_linear` means for the rules the
 * transpose family holds (`stdlib/rulesets/transpose.cml`: "the cotangent of the result => the
 * cotangent of the operand that carries the data"). A primitive whose other operand is not a
 * constant needs that operand's forward value, which is a residual and belongs to `split`; it is
 * refused by name (`skip=residual`) rather than answered with a wrong cotangent. Branches and
 * loops are refused the same way, by kind.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph.h"

#include <vector>

namespace camel::transforms {

/// The graph of `transpose(f)` and the program of the graphs built with it.
struct TransposeGraph {
    const camel::runtime::Program *program = nullptr;
    camel::runtime::GCGraph *graph         = nullptr;
};

/// The signature `transpose(f)` has for `function`, or nullptr when the function has no transpose:
/// `(ct: float) => (float)` for a function of one `float` parameter returning a `float`, since a
/// linear function's pullback takes the cotangent of the result and gives the cotangent of the
/// parameter.
///
/// `participates` turns it into the signature of a *partial* transposition: one flag per parameter,
/// true where a cotangent is answered for. A participating parameter is not a port -- the caller has no
/// value for it, because it is what the answer is a cotangent of -- so the signature is the values and
/// the cotangent in, one cotangent per participant out. Empty means every parameter participates, which
/// is the ordinary transposition.
/// `answersOf`, when given, is filled with the position of the parameter each member of the answer
/// tuple is the cotangent of, in the order the answer writes them. A caller that builds a call to
/// this member reads the positions here instead of deciding them again from the parameter list:
/// `xs[i]` has an `int` parameter, which the member reads as a value and answers nothing for, so the
/// answer's slots and the callee's parameters are not the same list.
camel::core::type::FunctionType *transposeSignature(
    camel::core::type::FunctionType *function, const std::vector<bool> &participates = {},
    std::vector<size_t> *answersOf = nullptr, bool honourClosedHalf = true);

/// Builds the graph of `transpose(f)` for the graph `function` of `program`. The graph `function`
/// is read, never changed. `os` receives what was built: which rule every primitive on the path was
/// transposed by, in the order the walk found them, and why it stopped when it did.
TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os);

} // namespace camel::transforms