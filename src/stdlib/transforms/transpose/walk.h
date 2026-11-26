// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What one transposition's walk keeps between its steps: the state a build shares with the callees
 * it has already transposed, the description of one call of a callee that answers the pair
 * (value, tangent), and the member builder that `transpose/entry.cpp` starts from.
 *
 * This is the transform's own plumbing, not its contract: the entry point and `TransposeGraph` are
 * declared in `transpose.h`.
 */

#pragma once

#include "transpose.h"

#include "lift.h"

#include "camel/core/context/context.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <iosfwd>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::transforms {

using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

namespace rt = camel::runtime;

/// What the graphs of one transpose share: one group (a callee's transpose is another member of
/// it, exactly as `jvp` lifts one graph per callee) and the callees already transposed, so that
/// two calls of one function call one graph.
struct TransposeState {
    camel::runtime::GraphDraftGroup &group;
    // program-holder-exempt: borrow-or-foreign-type -- the state borrows the program its pass
    // was given; the pass keeps it for the build.
    const camel::runtime::Program *program;
    std::unordered_map<uint32_t, GraphId> callees;
    /// The context whose rule table the transpose reads. `TransposeState` is where it belongs: it
    /// is what the build is handed (`buildTransposeGraph`), it is not a field of any value the
    /// language names, and the rule's right side is looked up by name in the table the context
    /// keeps.
    const camel::core::context::context_ptr_t &context;
    /// True inside a *probe*: the scratch build that discovers what a member needs, whose answer is
    /// thrown away with its draft (see `buildTransposeMember`). A probe never probes.
    bool inProbe = false;
    /// What each member needs, keyed by the graph whose member it is. A call site reads this before
    /// it computes the type it calls with: the capture ports are part of that type, so the set has
    /// to be known before the signature is written, not after the build that uses it.
    std::unordered_map<const camel::runtime::GCGraph *, std::vector<size_t>> calleeNeeds;
    /// Why the walk stopped, recorded at the site that stopped it. A probe carries a state of its
    /// own, so a probe's refusal never becomes the answer of the build that made it.
    TransposeRefusal refusal;
    /// The parameter whose *shape* the walk had to ask for and whose type does not state it: the
    /// cause the build refuses on when the member comes out with a port the operator's type has no
    /// room for. It is a fact and not a refusal: a call site inside a body carries the value (the
    /// capture is one of the member's ports, and the caller's type is written with it in hand), so
    /// asking is legitimate there and the walk must keep asking -- refusing at the step would break
    /// every `index_into` over an array whose extent only the caller knows. Whether the ask is
    /// fatal is a fact about the whole member, and `buildTransposeGraph` decides it once the ports
    /// are known: the shape has to be in the type or derivable from `ct`
    /// and known values -- and a value the caller carries *is* one.
    bool shapeWanted = false;
    /// The parameter the ask was about, and the name and type its declaration writes, for the
    /// sentence: the repair is that spelling. Recorded where the ask is made, because that is the
    /// one place that has the parameter and its declared type together.
    size_t shapeParam = 0;
    std::string shapeName;
    std::string shapeType;
};

/// One call of a callee that answers the pair (value, tangent), as a call site describes it to
/// `transposePairCall` below: the graph that is called, the call's own arguments, and the cotangent
/// of the pair's **tangent** component -- which is the only component a linear half has.
struct PairCall {
    camel::runtime::GCGraph *callee = nullptr;
    /// The source graph's call node: the place a refusal names.
    gc_node_ref_t ref = rt::kInvalidNodeRef;
    /// The call's ordinary arguments in the source graph, in the callee's parameter order.
    std::vector<gc_node_ref_t> arguments;
    /// The call's context (`with`) arguments in the source graph, in the callee's `with` order.
    std::vector<gc_node_ref_t> withArguments;
    /// This call's cotangent of the answer's tangent component.
    gc_node_ref_t cotangent = rt::kInvalidNodeRef;
};

/// One escaped cotangent: the product it is (`from`, the node whose rule handed it out) and the value
/// it landed on. Both are named because the refusal has to say which node escaped -- a value the
/// round only read is the only thing that may stand for it, and a reader that has the count alone
/// cannot tell that from a port that merely stands nearby.
struct Escape {
    gc_node_ref_t cotangent = rt::kInvalidNodeRef;
    gc_node_ref_t from      = rt::kInvalidNodeRef;
};

/// One arm's own state, borrowed: the region it walks, the stops it hands a cotangent out to, the
/// accumulators it fills inside that region (one per node, and one per stop), the slots a residual
/// goes back into, the escapes it caught, and whether it could place every cotangent it was handed.
/// The containers live in the arm's walk (`runSeeded`), because their length is a fact about the
/// region being read.
struct ArmWalk {
    uint32_t oldRegion                     = 0;
    const std::vector<gc_node_ref_t> *stops = nullptr;
    std::vector<gc_node_ref_t> *local       = nullptr;
    std::vector<bool> *hasLocal             = nullptr;
    std::vector<gc_node_ref_t> *portCotangents = nullptr;
    std::unordered_map<uint32_t, gc_node_ref_t> *residualBack = nullptr;
    std::unordered_map<gc_node_ref_t, Escape> *escapes        = nullptr;
    bool *armsOk                                              = nullptr;
    /// Whether a cotangent on a value outside the region may leave it through the capture channel:
    /// on while a loop's body runs, off for a branch arm, which has nowhere to put a capture.
    bool allowEscapes = false;
};

/// One value an arm's backward walk starts from: the value the arm answered and the cotangent it is
/// handed. A branch's arm is the walk seeded with one of these and stopped at the branch's own
/// region; a loop's body is the same walk with one per state it carries.
struct ArmSeed {
    gc_node_ref_t at        = rt::kInvalidNodeRef;
    gc_node_ref_t cotangent = rt::kInvalidNodeRef;
};

/// What the walk accumulates **about the member it is building**: the cotangent each source node
/// answered, whether it has one, the constants it forwarded, the members of an aggregate cotangent
/// it was handed, the values whose cotangent is zero because the value cannot hold one, the capture
/// and `with` ports its graph declares, the rules it used, and the extents the step under way was
/// handed.
///
/// It is in the context because the pieces that keep these tables are no longer written in the walk
/// (`transpose/walkmachinery.cpp`): a table is the walk's bookkeeping and not a node family's, and
/// one table read by many readers has to be one table. The vectors and maps themselves live where
/// they are sized -- the walk -- because their size is a fact about the graph being read; this
/// struct borrows them, exactly as it borrows `draft` and `forwardOf`.
struct WalkBookkeeping {
    /// How many nodes the source graph has: the length of every per-node table below.
    size_t nodeCount = 0;
    /// The cotangent of each source node, in the new graph; `kInvalidNodeRef` where it has none.
    std::vector<gc_node_ref_t> *cotangent = nullptr;
    /// Whether the node at that index has a cotangent. One flag per node rather than a scan of the
    /// table, because `kInvalidNodeRef` is also what a node whose cotangent a law answered nothing
    /// for holds.
    std::vector<bool> *hasCotangent = nullptr;
    /// The constant each source `Data` node was forwarded as, so two reads of one constant are one
    /// node of the new graph.
    std::vector<gc_node_ref_t> *constantOf = nullptr;
    /// The values whose cotangent is zero because the type has no tangent space, each recorded the
    /// first time it is met: one line per value, not one per read. This one list is the context's
    /// own -- it is not sized by the graph, and a reader of a zero contributes to it from wherever
    /// the zero is met.
    std::vector<gc_node_ref_t> zeroedValues;
    /// An aggregate value's cotangent, member by member, keyed by the value and not by the read.
    std::unordered_map<gc_node_ref_t, std::vector<gc_node_ref_t>> *memberCotangents = nullptr;
    /// The capture port each closure member of the source graph became, and the `with` port each
    /// `with` parameter became. A cotangent that lands on either is not the answer's.
    const std::unordered_map<gc_node_ref_t, gc_node_ref_t> *closurePortOf = nullptr;
    const std::unordered_map<gc_node_ref_t, gc_node_ref_t> *withPortOf = nullptr;
    /// The rules this member's answer used, in the order it first used them.
    std::vector<std::string> *usedRules = nullptr;
    /// The `int[]` of extents the step under way hands its law, when the law asks for one. Set by
    /// the operand family for the duration of its answer and cleared after it.
    gc_node_ref_t *extentsValue = nullptr;
};

/// What one node family is handed besides the node: the walk's state, the graph it is reading, the
/// stream a refusal is written to, the draft the member is being built into, and what the walk has
/// already forwarded. One struct for every family, because they differ in what they do and not in
/// what they are handed.
///
/// `draft` and `forwardOf` are pointers and not references because they do not exist when the
/// context is made: the transaction is opened and the forward table is sized further into the
/// walk, so the walk fills them in where they are made (`walk.cpp`) instead of here.
struct WalkContext {
    TransposeState &state;
    camel::runtime::GCGraph *function = nullptr;
    std::ostream &os;
    /// The draft the member is being written into. Read by the zero builders, which write a
    /// constant into this graph.
    camel::runtime::GraphDraft *draft = nullptr;
    /// What the walk forwarded for each source node, by the source node's own index; a node the
    /// cotangent path does not carry has no entry. A zero's shape is read from here.
    const std::vector<gc_node_ref_t> *forwardOf = nullptr;
    /// Whether some cotangent in this member could not be summed at all: a space with no addition,
    /// or a member of an aggregate that has one but no way to write it. It is a fact about the
    /// member and not a refusal -- the walk reads it at the end and refuses by name
    /// (`NoCotangentArithmetic`) instead of handing the slot a `float` sum -- and a probe carries a
    /// context of its own, so a probe's fact never becomes the answer of the build that made it.
    bool cotangentArithmeticMissing = false;
    /// The member being built: the id a law's right side is expanded into
    /// (`group.transaction(member)`), which is a fact about the build and not about a node.
    GraphId member = rt::kInvalidGraphId;
    /// Whether the answer is **partial**: some place is a value the caller fills and not a place a
    /// cotangent is answered for. Empty for an ordinary transposition, whose answer holds one
    /// cotangent per parameter that has a tangent space.
    bool partial = false;
    /// Which places the answer is about: the call site's set when it has one (read as written), and
    /// `places` -- the law's reading of a lifted signature -- otherwise.
    const std::vector<bool> *participates = nullptr;
    const LiftedParameters *places        = nullptr;
    /// The tables the walk keeps about the member it is building. Borrowed: the walk sizes them.
    WalkBookkeeping book;
};

/// Records the cause and writes the line for it, in the one place both are written: the cause on the
/// state (so the caller reads a value, not a sentence) and the sentence on the log. A site adds the
/// *where* -- the node, the uri, the callee -- because that is not the cause.
void refuseMember(WalkContext &ctx, TransposeRefusalCause cause,
                  std::optional<size_t> param = std::nullopt);

/// Records the parameter whose shape the walk had to ask for, and the name and type its declaration
/// writes: the parameter's place, its declared name, and the type whose extents are missing.
void noteUnstatedParameterOf(WalkContext &ctx, size_t param);

/// The cotangent space of a type: the space a value of that type's cotangent lives in, or nullptr
/// when the type has no tangent space at all. There is **no fallback** -- a value whose type has no
/// tangent space has no cotangent type, and every caller refuses by name; answering `float` here
/// would be the whole family's defect, because the answer is read.
Type *cotangentSpaceOfType(WalkContext &ctx, const Type *type);

/// The cotangent space of the value at `ref`, read from the value's type in the source graph: the
/// same question `jvp` asks of a parameter (`camel/core/tangent_space.h`), asked of a loop's state
/// and of a value that escaped it. A `float[]` state pulls back an array; a number pulls back a
/// number. Nothing decides that by hand -- the port, the tuple member and the component read all ask
/// here. A ref the source graph does not have has no type, and therefore no cotangent.
Type *cotangentTypeOfValue(WalkContext &ctx, gc_node_ref_t ref);

/// The zero of a cotangent space that the **type itself** states: an array of a floating scalar,
/// every element zero, at the extents the type states -- the same rule, in the same words, as
/// `jvp`'s `zeroConstantFor` (`modules/transforms/jvp.cpp`).
///
/// An extent the type leaves open has no size to write, so the answer there is `kInvalidNodeRef`:
/// the zero this member needs is the forward argument's, and its witness arrives as one of the cut's
/// residuals. An unwitnessed zero is wrong at every length but one, so a missing witness is a loud
/// refusal, never a quiet wrong width. `kInvalidNodeRef` also answers an array whose element is not
/// a numeric scalar, whose elements' zeros are the space's business and not one value's; the caller
/// refuses by name rather than answer a value the slot does not promise.
gc_node_ref_t zeroOfCotangentType(WalkContext &ctx, const Type *primal, Type *tangentType);

/// The zero of the cotangent space of the value at `ref`, asked of the space (`zeroOfSpaceOf`): a
/// floating scalar's is the constant the registry holds in that scalar's own width -- an `f32` zero
/// is four bytes, not a `float` zero read as four -- and a space whose zero is built from a value's
/// shape uses the space's own `zerosLikeUri`. Nothing here writes a `float` zero on behalf of a type
/// that is not a `float`.
///
/// The shape a zero array is built at is a node of **this** graph: the value the walk forwarded for
/// `valueRef` when it forwarded one, and the type's own stated zero (`zeroOfCotangentType`)
/// otherwise.
gc_node_ref_t zeroOfValue(WalkContext &ctx, gc_node_ref_t valueRef);

/// The sum of two cotangents in one space: what the place holds already (`into`, `kInvalidNodeRef`
/// when it holds nothing yet) and what arrives, answered as the value that place holds afterwards.
///
/// The sum is the space's **own** (`addUriOfSpace`, the registry's `addUri`), so an `f32` sums with
/// `:op/add_f`, an array with `:tangent/add_arr` and a tensor with `tensor:add`; nothing here spells
/// an addition of its own, and no width is written down as a placeholder. The space is the *value's*,
/// handed in by the caller that knows the value -- not `into`'s space: `into` is a node of the new
/// graph, and asking the source graph about it reads whatever source node sits at that index, a type
/// that has nothing to do with the cotangent being summed.
///
/// **The sum of an aggregate is its members' sum** (design principles 9.9: an aggregate's tangent is
/// its members' tangents, so its addition is the members' additions, one place at a time). It is
/// therefore derived from the type and not registered -- no tangent space has to spell an `addUri`
/// for a tuple or a struct, and `addUriOfSpace` answering nothing for one is not "an aggregate has no
/// addition", it is "a single operator cannot answer for it". A place whose member has no tangent
/// space (`int`, `bool`) carries the unit: the walk hands that place through untouched rather than
/// stopping the whole sum, and a member that has a space but no addition is a refusal about *that
/// member*, because the walk cannot sum what nothing can sum.
///
/// A space with no addition at all is a cotangent that cannot be summed: `cotangentArithmeticMissing`
/// records it and the member is refused by name (`NoCotangentArithmetic`) at the end of the walk,
/// rather than the slot being handed a `float` sum. This is a free function and not a `std::function`
/// because it recurses: an aggregate's members are summed by this same function, and a `const auto`
/// lambda cannot name itself while a free function needs no one to tell it its own type.
gc_node_ref_t sumOfCotangents(
    WalkContext &ctx, Type *space, gc_node_ref_t into, gc_node_ref_t node);

/// An aggregate of these values, as a node of the new graph, at the type the aggregate's tangent
/// space names: the Fill the builder writes for a tuple literal and for a struct literal
/// (`builder_static.cpp`, `{ a: .., b: .. }`), which is the shape every reader of an aggregate already
/// reads. The members are in the type's own order -- a struct's declaration order and a tuple's place
/// order, never the order the body happened to read them in -- and the slots are the identity, one
/// member per place, which is how `addAggregate` writes one (`modules/autodiff/pullback.cpp`).
///
/// The type is handed in rather than built here because the caller is the one that asked the tangent
/// space (`tangentSpaceOf`): a tuple of numbers is its own tangent, and so is a struct of them, while
/// a tuple of `f32` members is a tuple of `f32` and not one of `float`.
gc_node_ref_t aggregateOfMembers(
    WalkContext &ctx, Type *aggregateType, const std::vector<gc_node_ref_t> &parts);

/// What the cotangent bookkeeping answers, one question at a time. These are the walk's own
/// readings of a source graph -- which place the answer is about, which node is a constant, which
/// value can hold a cotangent, where a zero goes -- and they are asked by more than one node family,
/// which is why they are here and not a family's:
///
/// - `participatesInTheAnswer`: whether a cotangent landing on parameter `at` is one the answer
///   holds. The call site's set is read as written when it has one; without it the lift law's
///   reading decides (`places`), and a parameter outside the set is a value the graph reads.
/// - `isConstantNode` / `constantValueOf`: a `Data` node is a constant, and its value is what a rule
///   needs (`ct * c`). The node is forwarded once -- `book.constantOf` holds it -- so two reads of
///   one constant are one node of the new graph.
/// - `tupleOfValues`: a tuple of these values as a node of the new graph, at the type that names it.
///   The types travel with the values, because a state's cotangent can be an array and a tuple that
///   names it `float` is a tuple whose members are not the things it holds.
/// - `aggregateSlotCount`: how many places a value of aggregate type has. A `Fill` knows its
///   components; anything else has to be asked its type, and `tree::arity` is that one question.
/// - `holdsACotangent`: whether a value can hold one at all, asked of its `dataType` through
///   `tangentSpaceOf` -- the same query `jvp` asks of a parameter. This is **not** a reading of
///   which half of a signature a place is: it asks about one node's type, and a node with no type
///   yet is given the benefit of the doubt.
/// - `noteZeroContribution`: one line per value whose cotangent is zero because the value cannot
///   hold one, deduplicated: a value read in three rounds is still one value contributing one zero.
/// - `wantsCotangentAt`: whether a cotangent landing on `target` is one the answer holds. A closure
///   member's is not -- it is a known value, a constant of this differentiation, and no cotangent
///   has a slot to land in -- nor is a `with` port's.
/// - `addCotangentTo`: file one cotangent on a value, summing it with what the place already holds
///   through the space's own addition (`sumOfCotangents`). A cotangent no parameter asks for is not
///   collected at all, and a value with no tangent space records the zero it contributes instead.
/// - `noteRuleUsed`: the rules this member's answer used, once each, in first-use order. Each one is
///   also written as a `LAW_APPLIED` record (`transforms/law_readings.h`), which is the same fact in
///   the shape a caller keeps across members.
/// - `ruleCotangentOf`: one position's cotangent -- the rule the index filed for this operator, its
///   right side expanded where the operator stood, checked against the operand it answers for.
/// - `joinMemberReadOf`: the adjoint of reading a value member by member -- that value's own
///   cotangent, assembled member by member in the declaration's order (design principles 9.9), with
///   the zero of the member's own space where nobody read a member.
/// - `armCotangentOf`: a branch arm's answer, which is the walk seeded once and stopped at the arm's
///   own region.
bool participatesInTheAnswer(const WalkContext &ctx, size_t at);

bool isConstantNode(const WalkContext &ctx, gc_node_ref_t ref);

gc_node_ref_t constantValueOf(WalkContext &ctx, gc_node_ref_t ref);

std::pair<gc_node_ref_t, Type *> tupleOfValues(
    WalkContext &ctx, const std::vector<gc_node_ref_t> &parts,
    const std::vector<Type *> &partTypes);

size_t aggregateSlotCount(const WalkContext &ctx, gc_node_ref_t ref);

bool holdsACotangent(const WalkContext &ctx, gc_node_ref_t ref);

void noteZeroContribution(WalkContext &ctx, gc_node_ref_t target, gc_node_ref_t from);

bool wantsCotangentAt(WalkContext &ctx, gc_node_ref_t target);

void addCotangentTo(WalkContext &ctx, gc_node_ref_t ref, gc_node_ref_t node);

void noteRuleUsed(WalkContext &ctx, const std::string &name, std::string_view uri);

/// The rule `ruleName`'s answer at `position`, expanded at the types the step really has, for the
/// cotangent `ct` of the value at that position. `knownValue` hands over the value of an operand the
/// walk holds (the slot past the operands is the extents, `book.extentsValue`); `why` says what the
/// expansion could not build, `unboundSlot` names the operand it could not bind, and
/// `refusedAtTheLaw` records that this function itself refused (the port and answer checks) rather
/// than the expansion.
gc_node_ref_t ruleCotangentOf(
    WalkContext &ctx, const std::function<gc_node_ref_t(gc_node_ref_t)> &knownValue,
    const std::string &ruleName, std::span<const gc_node_ref_t> inputs, size_t position,
    gc_node_ref_t ct, Type *ctType, std::string &why, bool &noInstance, bool &refusedAtTheLaw,
    std::optional<size_t> &unboundSlot);

bool joinMemberReadOf(
    WalkContext &ctx, gc_node_ref_t ref, const std::vector<gc_node_ref_t> &slots);

gc_node_ref_t armCotangentOf(
    WalkContext &ctx,
    const std::function<
        std::vector<gc_node_ref_t>(uint32_t, const std::vector<ArmSeed> &,
                                   const std::vector<gc_node_ref_t> &)> &runSeeded,
    uint32_t oldRegion, gc_node_ref_t armResult, gc_node_ref_t armCotangent,
    const std::vector<gc_node_ref_t> &stops);

/// What one arm does with the cotangents it meets, one at a time:
///
/// - `addLocalCotangent`: file a cotangent on a value the arm **computes inside its region**: the
///   first one is what the place holds, and the next is the sum of the two (the space's own
///   addition, `sumOfCotangents`).
/// - `deliverCotangent`: hand a cotangent to `target`, which is one of four things -- a value the
///   round read that cannot hold a cotangent (zero, named), a value of the arm's own region (an
///   accumulator), one of the stops (the arm's answer for that value), a residual the arm read (the
///   slot it came from, by the slot's number), or -- while escapes are allowed -- a value from
///   outside the region, whose cotangent leaves the loop through the capture channel. Anything else
///   is a cotangent the arm cannot place, and that is said rather than dropped.
void addLocalCotangent(WalkContext &ctx, ArmWalk &arm, gc_node_ref_t ref, gc_node_ref_t node);

void deliverCotangent(
    WalkContext &ctx, ArmWalk &arm, gc_node_ref_t target, gc_node_ref_t node, gc_node_ref_t from);

/// An `int64` constant as a node of the new graph: the round count and the range's steps are
/// ordinary values of the graph this member builds, not numbers written into a payload.
gc_node_ref_t intConstantNode(WalkContext &ctx, int64_t value);

/// Whether `what` is one of the values a loop's states start from.
bool isStateInitialValue(const std::vector<gc_node_ref_t> &stateInitials, gc_node_ref_t what);

/// One component of a loop's answer, read at the type the region states for it: a component read is
/// typed like the member it reads (a sum over an array is an array), and an index this region has no
/// type for is a count that disagrees with the parts the region returns -- the walk's own defect --
/// so it answers no node rather than a `float` read.
gc_node_ref_t componentReadOf(
    WalkContext &ctx, const std::vector<Type *> &componentTypes, gc_node_ref_t loopNode,
    uint32_t index);

/// How a value is named in a diagnosis at a call: its index and its node kind, because two values
/// with one index can only be told apart by the kind of node one of them is (a port or a computed
/// value), and the sentence a reader gets has to say which of the two the call could not forward.
std::string describeNodeValue(const WalkContext &ctx, gc_node_ref_t value);

/// Why the value a call needs was not forwarded. The walk's last attempt is repeated with its
/// "stopped at a region" flag cleared, so the answer is this attempt's -- a failure is not memoised,
/// so the walk repeats the same path and stops at the same node. A value that comes out of a region
/// is one the walk would have to transpose the region for (`CallValueComputedByTheBody`); one that
/// does not is a value the call or its closure owes (`CallValueMissing`).
void noteUnforwardedCause(
    WalkContext &ctx, bool &stoppedAtARegion,
    const std::function<gc_node_ref_t(gc_node_ref_t)> &forward, gc_node_ref_t ref, size_t param);

/// The empty member `()` of an answer, at the type `unitType`: a parameter with no tangent space
/// answers it, and the signature has one place per parameter -- this is that place with nothing in
/// it -- which is what keeps the tuple the signature promised and the tuple the walk builds the same
/// length and the same order.
gc_node_ref_t unitMemberOf(WalkContext &ctx, Type *unitType);

/// Which declaration leaves one of the step's operands' shapes open, asked of every operand in the
/// order the step has them: the one walk-back `unstatedParameterBehind` (`transpose/signature.h`),
/// which the rule-expansion path asks too, so the two entrances to the cause name the same parameter
/// and cannot spell the same declaration two ways. Answering nothing means every operand's type
/// states its shape, which is what a step whose law answered does not need to ask at all.
std::optional<size_t> unstatedParameterOfAnyInput(
    const WalkContext &ctx, std::span<const gc_node_ref_t> inputs,
    const std::vector<size_t> &portParameter);

/// Builds the transposed member of one function into state's group and answers whether it did.
bool buildTransposeMember(
    TransposeState &state, camel::runtime::GCGraph *function, const GraphId member,
    std::ostream &os, const std::vector<bool> &participates = {},
    std::vector<size_t> *needs = nullptr);

} // namespace camel::transforms
