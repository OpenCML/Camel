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

#include "camel/core/context/context.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"

#include <cstddef>
#include <optional>
#include <iosfwd>
#include <ostream>
#include <string>
#include <unordered_map>
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

/// Builds the transposed member of one function into state's group and answers whether it did.
bool buildTransposeMember(
    TransposeState &state, camel::runtime::GCGraph *function, const GraphId member,
    std::ostream &os, const std::vector<bool> &participates = {},
    std::vector<size_t> *needs = nullptr);

} // namespace camel::transforms
