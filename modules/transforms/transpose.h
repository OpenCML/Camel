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
#include <string>
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
    /// The key holds a law, and the law was written for another shape. The index files a rule under
    /// the head of its left side and that head's operand count, and reads no operand types, so a
    /// key can hold one law for every shape its operator is called with -- `tensor:multiply` is
    /// called with two arrays and with an array and a number, and the law it files was written for
    /// two arrays. It is not `Rule`: there *is* a rule, and what it says is that this step is not
    /// the shape it was written for. Saying "no rule" here would be a sentence about a state the
    /// transform is not in.
    RuleShape,
    /// The law was found and its body has no instance at these operands: the key holds a law written
    /// for one operator, and a transform expands that law's right side at the operand types it really
    /// has (`ruleCotangent`). When the body is a dispatch on rank -- two cotangent helpers overloaded
    /// per shape, which is how one law serves a matrix product and its two vector lanes -- the ranks
    /// outside the set it serves are exactly what the expansion refuses.
    ///
    /// It is **not** `RuleShape`: there the law's *pattern* rejected the operand types, and the walk
    /// never built anything. Here the pattern took them, the law was chosen, and the law's own body is
    /// what has nothing to say for these ranks. The two want different edits: `RuleShape` is answered
    /// by widening the pattern, and this one by writing the missing shape's law. Measured 2026-11-03
    /// on `matmul_t` at a rank-three operand: with the law serving ranks one and two only, the
    /// refusal used to arrive as `CotangentNeedsAValue` carrying the builder's own sentence ("No
    /// matching function found for argument types"), which names a helper instead of the law and says
    /// nothing about the rank a reader has to change.
    RuleRanks,
    /// A parameter a cotangent is answered for, whose **value** the law's right side reads, and whose
    /// type does not state its shape (`float[..]`, `float[]`). The transposed function is handed the
    /// cotangent and nothing else, so a law that needs the parameter's value can only be answered
    /// when the shape the value carries is in the type: `float[2, 3]` says how many positions the
    /// parameter has, and `float[..]` does not. This is the language's rule about what a transposition
    /// is (author, 2026-11-03 16:15) -- the adjoint of a linear map needs the shape of its domain --
    /// not a limit of this walk: the same thing is why JAX's `linear_transpose` asks the caller for the
    /// shape of the value it transposes. The refusal is raised as its own diagnostic
    /// (`RuntimeDiag::TransposeNeedsParameterShape`), so a reader is told to write the shape in the
    /// type instead of being told the compiler cannot do it.
    ParameterShape,
    CotangentNeedsAValue,
    State,
    Range,
    CallNoTranspose,
    /// No program can reach this one today: the shape that reached it is the one `34788b554`
    /// fixed (a parameter's own value is carried into the closure), so it is kept to name a state
    /// the walk can still enter, not because a live check guards it.
    CallValueMissing,
    /// Reachable today (`vjp_loop.rounds_are_undone_in_reverse` records it): the value is one the
    /// function's own body computes, so carrying it means transposing that body.
    CallValueComputedByTheBody,
    UnreadParameter,
    NeedsAValueOutsideTheClosure,
    /// Two cotangents land on one value and that value's cotangent **space registers no addition**
    /// (`TangentSpace::addUri` is empty, or the type has no space in `DerivativeRegistry` at all), so
    /// the sum the walk has to write is not a thing the type has. It is not `UnreadParameter` -- the
    /// parameter *is* read, twice -- and it is not a width: the walk asked the space, exactly as it
    /// does for an `f32`, and the space answered that summing is not one of its operations. Answering
    /// `:op/add_d` in that place reads the value as a `float`, which is what this cause replaced.
    /// No case in the tree reaches it (every space the language registers for a differentiable value
    /// declares an addition); it exists so the state has a name instead of a `float` sum.
    NoCotangentArithmetic,
    /// The rule's cotangent port declares one type and the walk hands it another, with no conversion
    /// between them: the comparison the author asked for on 2026-11-04 (compare the port's declared
    /// type with the type of the value really handed, and refuse a disagreement). A law is filed
    /// under its operator's head and its operand count, so nothing about the index ties the law's
    /// cotangent port to the shape the step has: `mul_tangent`'s law writes `(ct: T[..])`, and at a
    /// `float` step the cotangent the member is handed is the number itself.
    ///
    /// **This is a compiler defect, not a language rule** (author, 2026-11-04, second ruling on the
    /// item): no program reaches it, because the cotangent a program can hand a transposed function is
    /// `Tangent<ResultOf<F>>` -- the type the declaration states -- so a port that disagrees with it is
    /// a law whose writing and whose instancing came apart inside the compiler.
    /// The cause stays as a guard against that inconsistency (the expansion reads the port's type to
    /// instance the law's right side and hands the value through it, which is how a wrong number or
    /// a bad read would get in), it is refused before anything is built, it carries both spellings
    /// (`portType`, `handedType`), and its sentence says whose fault it is. No case pins it: a case
    /// cannot reach it, and a criterion nothing reaches is not a criterion.
    CotangentPortMismatch,
};

/// The word the reading log spells `cause` as, or `nullptr` for `None`. The log's spelling and the
/// caller's decision are two readings of one value, so this is the only place that spells it.
const char *transposeRefusalWord(TransposeRefusalCause cause);

/// What a refusal is: the cause, the parameter it is about when the cause is about one, and the step
/// it was met at when it was met at a step. The index is the callee's parameter position as the graph
/// counts it; the sentence adds one because a reader counts from one.
///
/// `graph`, `node`, `uri` and `lookup` are the *where*, not the cause: the walk records them at the
/// site that stopped it, and the sentence reads them. They are here because a sentence about a step
/// with no node number and no operator name names no step -- "some step of some body has no
/// transpose rule" is true of every body and tells a reader nothing they can act on, while the log
/// line for the same refusal has carried `function=`, `node=`, `uri=` and `reason=` all along.
/// Empty/null means the walk had nothing to record there, and the sentence says that instead of
/// leaving the gap: a gap reads as "there is no such thing".
struct TransposeRefusal {
    TransposeRefusalCause cause = TransposeRefusalCause::None;
    std::optional<size_t> param;
/// The graph whose walk stopped, spelled as the reading log writes it (`function=<graph>`).
    std::string graph;
    /// The node that stopped the walk, in the graph's own numbering: the reading above the sentence
    /// numbers its nodes the same way, so a reader can find the step the sentence is about. Nothing
    /// for a refusal about the graph or a parameter rather than about a step.
    std::optional<size_t> node;
    /// That node's operator, when it has one: the key the law is filed under, so `Rule`'s lookup and
    /// `RuleShape`'s mismatch both name a thing a reader can look up instead of "this body".
    std::string uri;
    /// Why a `Rule` refusal found no rule: the log's own word for the lookup (`unbound`,
    /// `no-such-family`, `no-rule`), or empty for any other cause. Kept as the log's word rather
    /// than as a second enum, so that one fact is spelled once.
    std::string lookup;
    /// What the law takes and what the step has (`RuleShape`), in the index's own spelling: the one
    /// fact that turns "another shape" into something a reader can act on.
    std::string shape;
    /// The law whose body has no instance (`RuleRanks`): the name a reader greps in the ruleset.
    std::string rule;
    /// The operands' ranks (`RuleRanks`), in the law's own order, spelled `(3, 2)`: the fact the
    /// refusal is about, because a law that dispatches on rank serves a set of them.
    std::string ranks;
    /// The parameter's name as the declaration writes it (`x`), for the cause that is about one
    /// (`ParameterShape`): the sentence names the parameter a reader has to edit, and the position
    /// alone would leave them counting parameters.
    std::string paramName;
    /// The parameter's declared type, spelled as the program wrote it (`float[..]`, `float[2, 3]`),
    /// for the causes that are about a parameter's type rather than about a step (`ParameterShape`):
    /// the repair is an edit to this spelling, so a reader is handed it verbatim.
    std::string paramType;
    /// The type the rule's cotangent port declares (`CotangentPortMismatch`), spelled as the law
    /// writes it: the port is the line a reader edits, so it is handed over verbatim.
    std::string portType;
    /// The type of the value the walk really hands that port, spelled the same way. The other half of
    /// the same fact, and the one the program wrote: a reader compares the two and knows which side
    /// moved. Empty for every cause but `CotangentPortMismatch`.
    std::string handedType;
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
///
/// `promisedToTheOperator` says who the answer is for, and it is the difference between a refusal and
/// a working member. The `transpose` operator's call site is typed from `transposeSignature` before any
/// body is read, so the graph it gets has to be the function that type names: a member with a port
/// more is a function the call site cannot call, and the build refuses it rather than let it reach the
/// run (`ParameterShape` when the extra port carries the shape of a parameter whose type leaves it
/// open, `NeedsAValueOutsideTheClosure` otherwise). A walk that builds a member for a *call inside a
/// body* is not promised anything: its caller writes the callee's type with the captures in hand, so
/// the extra ports are filled, and comparing the counts there would refuse an `index_into` over an
/// array whose extent only the caller knows. The kernel passes true; nothing else does.
TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os, bool promisedToTheOperator = false);

} // namespace camel::transforms