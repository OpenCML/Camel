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

#include <cstddef>
#include <optional>
#include <vector>

namespace camel::transforms {

/// Why `transpose(f)` refused the function.
///
/// This is the fact, and it is said once. `buildTransposeMember` records it where the walk stopped,
/// `transposeRefusalWord` spells it into the reading log, and the caller reads *this* to know what to
/// tell a reader -- no one parses the log back into a cause. A cause added here without a word, a
/// sentence and a site that records it is a red in `test/tools/transpose_refusal/check.py`, which
/// holds the four sets together.
///
/// `None` is "the build refused and recorded nothing": it is what an attempt that stopped before any
/// site ran leaves, and it keeps the sentence that names no cause instead of inventing one.
enum class TransposeRefusalCause {
    None,
    Signature,
    Exit,
    Output,
    Component,
    Join,
    Kind,
    Condition,
    ArmTangents,
    Arm,
    Rule,
    Arity,
    CotangentNeedsAValue,
    State,
    StateUnread,
    Range,
    CallNoTranspose,
    CallValueMissing,
    CallValueComputedByTheBody,
    UnreadParameter,
    NeedsAValueOutsideTheClosure,
};

/// The word the reading log spells `cause` as, or `nullptr` for `None`. The log's spelling and the
/// caller's decision are two readings of one value, so this is the only place that spells it.
const char *transposeRefusalWord(TransposeRefusalCause cause);

/// What a refusal is: the cause, and the parameter it is about when the cause is about one. The
/// index is the callee's parameter position as the graph counts it; the sentence adds one because a
/// reader counts from one.
struct TransposeRefusal {
    TransposeRefusalCause cause = TransposeRefusalCause::None;
    std::optional<size_t> param;
};

/// The graph of `transpose(f)` and the program of the graphs built with it. `refusal` is what the
/// build said when `graph` is null: a null graph on its own would leave the caller with the log
/// string as its only account of why.
struct TransposeGraph {
    const camel::runtime::Program *program = nullptr;
    camel::runtime::GCGraph *graph         = nullptr;
    TransposeRefusal refusal;
};

/// The signature `transpose(f)` has for `function`, or nullptr when the function has no transpose:
/// `(ct: float) => (float)` for a function of one `float` parameter returning a `float`, and
/// `(ct: float) => (float, float)` for one of two -- the cotangent of the result in, one cotangent per
/// parameter out, always as a tuple, because the law states one shape for every arity (author,
/// 2026-11-02 11:30). Every parameter of `f` is a linear input, so no parameter is a value the call
/// passes; a value the body needs is captured by the function that is transposed.
///
/// `participates` is the one exception, and it belongs to a call site rather than to a type: one flag
/// per parameter, true where a cotangent is answered for. A parameter outside the set is a value the
/// caller still passes, which is the shape a rule's tangent function has. Empty means every parameter
/// participates, which is the ordinary transposition.
/// `answersOf`, when given, is filled with the position of the parameter each member of the answer
/// tuple is the cotangent of, in the order the answer writes them. A caller that builds a call to
/// this member reads the positions here instead of deciding them again from the parameter list:
/// `xs[i]` has an `int` parameter, which the member reads as a value and answers nothing for, so the
/// answer's slots and the callee's parameters are not the same list.
camel::core::type::FunctionType *transposeSignature(
    camel::core::type::FunctionType *function, const std::vector<bool> &participates = {},
    std::vector<size_t> *answersOf = nullptr,
    const std::vector<size_t> *captures = nullptr);

/// Builds the graph of `transpose(f)` for the graph `function` of `program`. The graph `function`
/// is read, never changed. `os` receives what was built: which rule every primitive on the path was
/// transposed by, in the order the walk found them, and why it stopped when it did.
TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os);

} // namespace camel::transforms