// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The transpose of a linear function's graph: `transpose(f)`.
 *
 * Where `jvp` builds a graph that runs the data flow forward with a tangent beside it, this builds
 * a graph that runs the data flow the other way: the caller passes the cotangent of the result, and
 * what comes out is the cotangent of the parameter. Nothing of `f`'s graph is changed -- the same
 * rule that makes `reinterp` produce a new function applies here, and for the same reason: a
 * published graph is never replaced.
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
#include <utility>
#include <vector>

namespace camel::transforms {

/// Why `transpose(f)` refused the function.
///
/// This is the fact, and it is said once. `buildTransposeMember` records it where the walk stopped,
/// `transposeRefusalWord` spells it into the reading log, and the caller reads *this* to know what
/// to tell a reader -- no one parses the log back into a cause. A cause added here without a word,
/// a sentence and a site that records it is a red in `test/tools/transpose_refusal/check.py`, which
/// holds the four sets together.
///
/// `None` is "the build refused and recorded nothing": it is what an attempt that stopped before
/// any site ran leaves, and it keeps the sentence that names no cause instead of inventing one.
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
    /// The law was found and its body has no instance at these operands: the key holds a law
    /// written for one operator, and a transform expands that law's right side at the operand types
    /// it really has (`ruleCotangent`). When the body is a dispatch on rank -- two cotangent
    /// helpers overloaded per shape, which is how one law serves a matrix product and its two
    /// vector lanes -- the ranks outside the set it serves are exactly what the expansion refuses.
    ///
    /// It is **not** `RuleShape`: there the law's *pattern* rejected the operand types, and the
    /// walk never built anything. Here the pattern took them, the law was chosen, and the law's own
    /// body is what has nothing to say for these ranks. The two want different edits: `RuleShape`
    /// is answered by widening the pattern, and this one by writing the missing shape's law.
    /// On `matmul_t` at a rank-three operand, with the law serving ranks one
    /// and two only, a refusal would arrive as `cotangent-needs-a-value` carrying the builder's
    /// own sentence ("No matching function found for argument types"), which names a helper instead
    /// of the law and says nothing about the rank a reader has to change.
    RuleRanks,
    /// A declaration that states no shape, where a transposition needs one: a parameter whose
    /// extents are open (`float[..]`, `float[]`) and whose value -- or, what is the same question
    /// here, the shape that value carries -- a law's right side has to read. The transposed
    /// function is handed the cotangent and nothing else, so such a law can only be answered when
    /// the shape is in the type: `float[2, 3]` says how many positions the parameter has, and
    /// `float[..]` does not. This is the language's rule about what a transposition is -- the adjoint of a linear
    /// map needs the shape of its domain -- not a limit of this walk: the same thing is why JAX's `linear_transpose` asks the caller for the
    /// shape of the value it transposes. The refusal is raised as its own diagnostic
    /// (`RuntimeDiag::TransposeNeedsParameterShape`), so a reader is told to write the shape in the
    /// type instead of being told the compiler cannot do it.
    ///
    /// **One class, one word: this is the word for "the declaration states no shape"**. The same
    /// fact arrives at two entrances and must not be spelled two ways: the
    /// operator's own call site refused when the member came out with a port the promised type has
    /// no room for (`buildTransposeGraph`), and a law's right side that could not be handed an
    /// operand reached through `vjp`/`split` arrived as `cotangent-needs-a-value` ("a value is
    /// missing"). The second spelling would teach readers to look for a lost value while the
    /// repair is an extents write, so the walk asks the same question at that site and refuses
    /// *this* cause: the operand the law read is an array whose declaration leaves its extents
    /// open, which is the shape this diagnostic names. A cause is one class of fact, and this class
    /// has one word.
    ParameterShape,
    /// A law's right side reads a **value** the walk cannot hand it, and no declaration of a shape
    /// supplies that value: the parameter's own value is the tangent's (a transposition answers the
    /// tangent, it is not handed one), or the operand is a number whose shape is already complete
    /// (`float` needs no extents, so no edit to a declaration adds anything). For example
    /// `mul_tangent`'s law reads `w` and `dx`, both tangents, and `read_outer`'s `k = x*x` reads a
    /// `float` parameter -- `skip=law-reads-a-value uri=:op/mul_d position=0`.
    ///
    /// It is **not** `ParameterShape`, and the two must not be merged: there the repair is an edit
    /// to a declaration, and here there is no edit to make -- the value has to reach the member
    /// some other way. A reader who is told to write extents in a declaration that already states them all
    /// has been told to do something that changes nothing.
    LawReadsAValue,
    /// A call whose answer is the `(value, tangent)` pair is read as a whole, and this step has no
    /// cotangent on the pair's tangent component: there is no rule for a cotangent arriving on a
    /// pair, so the call cannot be turned around by reading one component's cotangent as another's.
    /// Its own cause and not `LawReadsAValue`: nothing here reads a value the walk failed to reach
    /// -- the walk never reached a *cotangent*, and the sentence has to say so.
    ///
    /// **No case in the tree reaches it** (the step's own log field,
    /// `why=no-cotangent-on-the-tangent-component`, occurs at the site in
    /// `transpose.cpp#buildTransposeMember` and in no case, plan or page). It is kept as a guard
    /// rather than deleted, the way `CallValueMissing` is: a pair read with a cotangent wanted on
    /// its second component is a state the walk can still enter, and answering it with a cause
    /// about a missing *value* is what sent a reader to the wrong edit. A program that reaches it
    /// needs a page of its own before this can be called a language rule.
    CotangentOnAPair,
    State,
    Range,
    CallNoTranspose,
    /// No program can reach this one today: the shape that reached it is the one fixed
    /// by carrying a parameter's own value into the closure, so it is kept to name a state
    /// the walk can still enter, not because a live check guards it.
    CallValueMissing,
    /// Reachable today (`vjp_loop.rounds_are_undone_in_reverse` records it): the value is one the
    /// function's own body computes, so carrying it means transposing that body.
    CallValueComputedByTheBody,
    UnreadParameter,
    NeedsAValueOutsideTheClosure,
    /// Two cotangents land on one value and that value's cotangent **space registers no addition**
    /// (`TangentSpace::addUri` is empty, or the type has no space in `DerivativeRegistry` at all),
    /// so the sum the walk has to write is not a thing the type has. It is not `UnreadParameter` --
    /// the parameter *is* read, twice -- and it is not a width: the walk asked the space, exactly
    /// as it does for an `f32`, and the space answered that summing is not one of its operations.
    /// Answering
    /// `:op/add_d` in that place reads the value as a `float`, which is what this cause replaced.
    /// No case in the tree reaches it (every space the language registers for a differentiable
    /// value declares an addition); it exists so the state has a name instead of a `float` sum.
    NoCotangentArithmetic,
    /// The rule's cotangent port declares one type and the walk hands it another, with no
    /// conversion between them: the comparison of the
    /// port's declared type with the type of the value really handed, refusing a disagreement. A
    /// law is filed under its operator's head and its operand count, so nothing about the index
    /// ties the law's cotangent port to the shape the step has: `mul_tangent`'s law writes `(ct:
    /// T[..])`, and at a `float` step the cotangent the member is handed is the number itself.
    ///
    /// **This is a compiler defect, not a language rule** : no program reaches it, because the cotangent a program can hand a transposed function
    /// is `Tangent<ResultOf<F>>` -- the type the declaration states -- so a port that disagrees
    /// with it is a law whose writing and whose instancing came apart inside the compiler. The
    /// cause stays as a guard against that inconsistency (the expansion reads the port's type to
    /// instance the law's right side and hands the value through it, which is how a wrong number or
    /// a bad read would get in), it is refused before anything is built, it carries both spellings
    /// (`portType`, `handedType`), and its sentence says whose fault it is. No case pins it: a case
    /// cannot reach it, and a criterion nothing reaches is not a criterion.
    CotangentPortMismatch,
    /// The type a law's right side **answers** with is not the type of the operand whose cotangent
    /// it claims to be (a transposition checks, as it runs, the type of each law's right side against
    /// the type of the operand it answers for -- shape and rank -- and names the law where the two
    /// disagree). A rule's right side answers one component per port, in
    /// the order its left side writes them, and nothing ties that component's shape to the
    /// operand's: the index files a law under its operator's head and its operand count and reads no
    /// operand types, so a law can hand back the whole broadcast result for an operand the broadcast
    /// spread one element of. Without this cause, in `add_tensor_t` and `subtract_t` with
    /// `x: float[2, 3]`, `b: float[3]`, `y = x + b`, `pullback(1.0)` would answer `dL/db` as a
    /// `[[1, 2, 3], [4, 5, 6]]` tensor while the static type said `float[3]`, with exit 0 and no
    /// diagnostic -- a wrong number under a right type, which is the one reading the language
    /// forbids (design principles 9.9: give the correct derivative or refuse by name).
    ///
    /// It is **not** `CotangentPortMismatch`: that one compares the law's *input* port with the
    /// cotangent the walk hands it and is a compiler defect no program reaches; this one compares
    /// the law's *output* with the operand it is the cotangent of, and a program reaches it through
    /// any operator a law was written for too wide. Nor is it `RuleShape` (the law's pattern
    /// rejecting the operand types, before anything is built) or `RuleRanks` (the law's body having
    /// no instance at these operands): here the law was chosen, its body ran, and what it answered
    /// is a different shape from the one it is answering for.
    ///
    /// What is compared is what the two types **state** (`statedShapeDisagreement`, `transpose.cpp`):
    /// an extent a type leaves open is not compared, and a side whose rank is open (`T[..]`, which
    /// includes rank 0) is not compared on rank. So a law that reduces with `sum_to` -- the repair
    /// this cause was added beside -- answers `T[..]` and says nothing here.
    LawAnswerShape,
    /// The function this call applies is **built by the body out of a parameter** of the function
    /// being transposed: `f(x) = mk(x)(2.0)` with `mk(x) = (y) => x * y`, where the value the call
    /// applies is `mk`'s answer, assembled from `x`. A bare `transpose(f)` treats every parameter
    /// of `f` as a linear input, and this call is not one: what it applies was *made* out of a
    /// parameter, which makes the call a second-order use of that parameter and `f` not linear in
    /// it (a callee built from a linear input is a **linearity** refusal -- the
    /// sentence names the parameter, because "the answer has no source" describes this walk and not
    /// the language). `param` is the parameter's index, so the sentence can name it.
    CalleeBuiltFromAParameter,
};

/// The word the reading log spells `cause` as, or `nullptr` for `None`. The log's spelling and the
/// caller's decision are two readings of one value, so this is the only place that spells it.
const char *transposeRefusalWord(TransposeRefusalCause cause);

/// What a refusal is: the cause, the parameter it is about when the cause is about one, and the
/// step it was met at when it was met at a step. The index is the callee's parameter position as
/// the graph counts it; the sentence adds one because a reader counts from one.
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
    /// That node's operator, when it has one: the key the law is filed under, so `Rule`'s lookup
    /// and `RuleShape`'s mismatch both name a thing a reader can look up instead of "this body".
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
    /// What the law's body could not be instantiated as, in the words the call's candidate search
    /// wrote (`RuleRanks`): the helper the law calls and the argument types it was handed
    /// (`No candidate of 'spread_over' applies to (float[3], int[])`), which is the fact that turns
    /// "the body has no instance" into an edit. Empty when the expansion wrote no words.
    std::string expansion;
    /// The parameter's name as the declaration writes it (`x`), for the cause that is about one
    /// (`ParameterShape`): the sentence names the parameter a reader has to edit, and the position
    /// alone would leave them counting parameters.
    std::string paramName;
    /// The parameter's declared type, spelled as the program wrote it (`float[..]`, `float[2, 3]`),
    /// for the causes that are about a parameter's type rather than about a step
    /// (`ParameterShape`): the repair is an edit to this spelling, so a reader is handed it
    /// verbatim.
    std::string paramType;
    /// The type the rule's cotangent port declares (`CotangentPortMismatch`), spelled as the law
    /// writes it: the port is the line a reader edits, so it is handed over verbatim.
    std::string portType;
    /// The type of the value the walk really hands that port, spelled the same way. The other half
    /// of the same fact, and the one the program wrote: a reader compares the two and knows which
    /// side moved. Empty for every cause but `CotangentPortMismatch`.
    std::string handedType;
    /// The type the law's right side answered with (`LawAnswerShape`), as this walk read it off the
    /// node the expansion built. The repair is an edit to the law's right side, so the spelling is
    /// the law author's, not the program's.
    std::string answerType;
    /// The type of the operand that answer claims to be the cotangent of, in the program's own
    /// spelling. The other half of the same fact: a reader compares the two and knows what the law
    /// was supposed to answer.
    std::string answeredOperandType;
};

/// The graph of `transpose(f)` and the program of the graphs built with it. `refusal` is what the
/// build said when `graph` is null: a null graph on its own would leave the caller with the log
/// string as its only account of why.
///
/// `program` is held through `ProgramHold`: the group that reserved it is gone by the time this
/// value reaches the caller, a program a `reserve` made while a program runs is one the manager
/// does not root (a run roots what a function value names, program.cpp), and a safepoint between
/// the two would free the program `graph` is a graph of.
struct TransposeGraph {
    camel::runtime::ProgramHold program;
    camel::runtime::GCGraph *graph = nullptr;
    TransposeRefusal refusal;

    TransposeGraph() = default;
    /// A build that refused: there is no program to root, and `why` says what stopped it.
    explicit TransposeGraph(TransposeRefusal why) : refusal(std::move(why)) {}
    TransposeGraph(
        camel::core::context::Context &context, const camel::runtime::Program *program,
        camel::runtime::GCGraph *graph)
        : program(context, program), graph(graph) {}
};

/// The signature `transpose(f)` has for `function`, or nullptr when the function has no transpose:
/// `(ct: float) => (float)` for a function of one `float` parameter returning a `float`, and
/// `(ct: float) => (float, float)` for one of two -- the cotangent of the result in, one cotangent
/// per parameter out, always as a tuple, because the law states one shape for every arity. Every parameter of `f` is a linear input, so no parameter is a value the call
/// passes; a value the body needs is captured by the function that is transposed.
///
/// `participates` is the one exception, and it belongs to a call site rather than to a type: one
/// flag per parameter, true where a cotangent is answered for. A parameter outside the set is a
/// value the caller still passes, which is the shape a rule's tangent function has. Empty means
/// every parameter participates, which is the ordinary transposition. `answersOf`, when given, is
/// filled with the position of the parameter each member of the answer tuple is the cotangent of,
/// in the order the answer writes them. A caller that builds a call to this member reads the
/// positions here instead of deciding them again from the parameter list: `xs[i]` has an `int`
/// parameter, which the member reads as a value and answers nothing for, so the answer's slots and
/// the callee's parameters are not the same list.
camel::core::type::FunctionType *transposeSignature(
    camel::core::type::FunctionType *function, const std::vector<bool> &participates = {},
    std::vector<size_t> *answersOf = nullptr, const std::vector<size_t> *captures = nullptr);

/// Why this transform has no cotangent for a result of type `result`, in the words the call site's
/// diagnosis uses; empty when it has one. The call site asks this for its message and the gate asks
/// it for its answer, so the judgement and the refusal cannot disagree.
std::string resultCotangentRefusal(const camel::core::type::Type *result);

/// Builds the graph of `transpose(f)` for the graph `function` of `program`. The graph `function`
/// is read, never changed. `os` receives what was built: which rule every primitive on the path was
/// transposed by, in the order the walk found them, and why it stopped when it did.
///
/// `promisedToTheOperator` says who the answer is for, and it is the difference between a refusal
/// and a working member. The `transpose` operator's call site is typed from `transposeSignature`
/// before any body is read, so the graph it gets has to be the function that type names: a member
/// with a port more is a function the call site cannot call, and the build refuses it rather than
/// let it reach the run (`ParameterShape` when the extra port carries the shape of a parameter
/// whose type leaves it open, `NeedsAValueOutsideTheClosure` otherwise). A walk that builds a
/// member for a *call inside a body* is not promised anything: its caller writes the callee's type
/// with the captures in hand, so the extra ports are filled, and comparing the counts there would
/// refuse an `index_into` over an array whose extent only the caller knows. The kernel passes true;
/// nothing else does.
TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os, bool promisedToTheOperator = false);

} // namespace camel::transforms