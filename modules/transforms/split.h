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
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"

#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace camel::transforms {

/// The two halves of a lifted function, as types. `split(f)` answers the **forward** half: calling it
/// gives the value and the linear function `lin` closed over the residuals. So `forward` is
/// `(values...) => (result, lin)`, `backward` is `lin`'s own type `(tangents...) => tangent` -- with no
/// residual position in it -- and `closure` is the layout of `lin`'s closure tuple, one member per
/// residual, which the build reads off the backward graph. `pair` is the type the call `split(f)`
/// answers with, i.e. the forward half. `why` says why there are none when the function is outside
/// what the step takes.
///
/// The **forward** half is the one that carries the source's `with` list; `backward` carries none
/// (author, 2026-11-03 15:15). A `with` value the backward half reads is a residual, so it arrives
/// in `lin`'s closure, and `lin` stays an ordinary function of the tangents -- which is what makes
/// `transpose(lin)` the declared `Pullback<F>` `(ct) => (..Tangent<ParamsOf<F>>)`.
struct SplitHalves {
    camel::core::type::FunctionType *forward    = nullptr;
    camel::core::type::FunctionType *backward   = nullptr;
    const camel::core::type::TupleType *closure = nullptr;
    const camel::core::type::Type *pair         = nullptr;
    std::string why;
};

/// The halves of a lifted function: every ordinary parameter is the pair `(value, tangent)` and the
/// exit is the pair `(result, tangent)`, so the counts are even in and two out. nullptr halves when
/// the function is not of that shape.
SplitHalves splitHalvesOf(camel::core::type::FunctionType *function);

/// The halves when the residuals are known -- which they are as soon as the *graph* is in hand,
/// because which values cross the cut is a property of the body (`residualsOf`). `split` builds its
/// halves with this one, so `lin` captures the values the graph names: none for the identity graph,
/// five for `f(x, y) = x*x*y`. `splitHalvesOf(function)` is the same call with no graph, i.e. an empty
/// closure -- the shape the operator resolver checks the call site against, since a signature has
/// nothing to name.
SplitHalves splitHalvesWith(
    camel::core::type::FunctionType *function, camel::core::type::Type *residuals);

/// One residual: a value the forward half computes and the backward half reads, so that the cut
/// between the two graphs carries it instead of recomputing it. A `with` port the backward half reads
/// is one of these even though nothing computes it: the forward half holds the port that receives the
/// value, and that port is what crosses.
struct Residual {
    /// The node of the lifted graph that computes the value -- a parameter port, a `with` port, or a
    /// node the forward half keeps.
    camel::runtime::gc_node_ref_t node = camel::runtime::kInvalidNodeRef;
    camel::core::type::Type *type      = nullptr;
    /// The parameter it is the value of, when it is a parameter; `kNotAParameter` when it is a value
    /// the forward half computes.
    size_t parameter = 0;
    /// When this residual is the **linear half** of a call whose callee answers a pair: `node` is the
    /// call, and `type` is that half's type -- the callee's own backward half, `(tangents) => tangent`,
    /// with the values it reads in its closure. Nothing the callee computes crosses as a value: the
    /// call site never sees the residuals of `g`'s body (author, 2026-11-03 17:55), it sees `lin_g`.
    /// The two halves reach it differently: the forward half gets it as the second component of the
    /// call it placed (`ACCS 1`), and the backward half **calls** it on the tangents the source call
    /// passed, which is one indirect call where the source had one direct one.
    bool linearHalf = false;
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
/// into the forward half. A `with` port the tangent side reads crosses the same way, whether or not
/// the result side reads it too (author, 2026-11-03 15:15: 后一半要读的东西都是残差，`with` 值也在内).
///
/// Program order: parameters by their index, then computed values by their node. The answer is empty
/// when the graph is not a lifted graph (the exit is not the pair), which is when there is nothing
/// to cut.
std::vector<Residual> residualsOf(
    const camel::runtime::GCGraph &lifted, const camel::runtime::Program *program = nullptr);

/// Whether this graph answers the pair (value, tangent) that `split` cuts -- **by its content**, not by
/// any record of where it came from. Two questions, both of them, because either one alone names a
/// different class of functions:
///
///  - the **signature** says the parameters come in two halves of equal length and the exit is a
///    two-member tuple (`splitHalvesWith`: an even, non-zero parameter count and a `Tuple` of two);
///  - the **graph** says the exit is that pair -- a two-member `Fill` of the tuple kind, a loop whose
///    value is the pair, or the `Join` a branch answers with (`exitShapeOf`).
///
/// A hand-written graph of that content is the same function as a lifted one and is answered the same
/// way (author, 2026-11-03 16:55): the rule a caller applies to a callee must be a function of the
/// callee's content, or two graphs that compute the same thing are transformed differently because of
/// where one of them was written down. `why` says which of the two questions failed.
///
/// What this function does **not** answer is whether the cut can be built: a graph may answer the pair
/// and still hold a construct the cut refuses. That is `buildSplitGraph`'s answer, and a caller that
/// reads this one must hand the refusal on by name rather than fall back to another rule.
bool answersAPair(const camel::runtime::GCGraph *callee, std::string *why = nullptr);

/// `lin`: `backwardId`'s graph as a function value closed over `residuals` -- a FILL of the
/// `FunctionClosure` kind whose static input names the graph and whose `with` inputs are the values in
/// the closure's order. The one writer of that shape: the cut uses it to hand the linear half out of
/// the forward half, and a call site that transposes a pair-answering callee uses it to close the same
/// graph over the values it stands on, so the two are the same value and neither states a count.
camel::runtime::gc_node_ref_t buildLinearClosure(
    camel::runtime::GraphDraft &draft, const camel::runtime::Program *program,
    camel::runtime::GraphId backwardId, const camel::core::type::TupleType *closure,
    camel::core::type::FunctionType *linType,
    std::span<const camel::runtime::gc_node_ref_t> residuals);

/// The two graphs of `split(f)` and the program of them.
struct SplitGraph {
    const camel::runtime::Program *program = nullptr;
    camel::runtime::GCGraph *forward       = nullptr;
    camel::runtime::GCGraph *backward      = nullptr;
    /// The programs of the cuts this one **stands on**: the group a callee's cut was built in, and --
    /// one level down -- the programs *that* build adopted, in the order they were taken. A cut whose
    /// value half calls a pair-answering callee answers the call with that callee's forward half, and
    /// that half may itself have been answered by a call of *its* callee's forward half. The graphs
    /// of the first level travel with `program`; the graphs of the deeper levels are listed here, and
    /// they are what a caller of a cut one level out cannot find in its own program
    /// (`Program::adoptMissing` copies only the graphs the program it reads *owns*, and a graph a
    /// build adopted is not one it owns). Empty for every cut whose callees answer the pair
    /// themselves -- which is every cut in the tree that does not nest twice.
    std::vector<const camel::runtime::Program *> nestedPrograms;
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

} // namespace camel::transforms