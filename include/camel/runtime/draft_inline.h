// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Runtime draft inline helpers.
 *
 * This layer turns the low-level clone primitive into a reusable inline splice
 * operation on GraphDraft. Passes should not manually clone a callee, bind
 * ports, redirect users, and erase the old FUNC node in ad-hoc order; that
 * sequence is now centralized here.
 */

#pragma once

#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"

namespace camel::runtime {

struct DraftInlineResult {
    gc_node_ref_t callNode  = kInvalidNodeRef;
    gc_node_ref_t valueExit = kInvalidNodeRef;
    gc_node_ref_t ctrlEntry = kInvalidNodeRef;
    gc_node_ref_t ctrlExit  = kInvalidNodeRef;

    explicit operator bool() const { return valueExit != kInvalidNodeRef; }
};

/// With `asArm`, `funcNodeId` is the call of a branch arm the front end built as a graph: its body
/// becomes the Arm region of that arm (an arm index of the BRCH that orders the call), not an Inline
/// region.
DraftInlineResult inlineCallableInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t funcNodeId,
    bool asArm = false);
/// Runaway guard on the type-only specializations of one recursive function that a program may
/// hold.
///
/// A specialization that only refines parameter types (a tensor argument whose type carries its
/// shape) re-infers the callee's types, so the recursive call inside the copy can carry yet
/// another type. Reading E bounds that recursion in the types themselves: a recursive call inside
/// an instance of the function it names reuses that instance, and a call whose key differs
/// (polymorphic recursion) has its extents and dimension names widened to `?` instead of taking a
/// copy per level. No count is a shape bound any more, and nothing stops silently at one: passing
/// this guard means the specialization never reached a fixed point, which is reported as a
/// compiler error naming the function -- never a truncated shape.
inline constexpr size_t kTypeSpecializationFixedPointGuard = 4096;

/// With `functionValuesOnly`, the call is specialized on the function value a function-typed
/// argument holds and on nothing else: that parameter becomes that function in the copy, the
/// calls of it inside the copy are direct calls once devirtualization has run there, and no
/// constant argument or refined parameter type is bound. This is what defunctionalization needs
/// and an optimizer does not: one copy per (callee, function) pair, and no recursion unrolled on
/// a constant. Off, a GC-managed argument stays dynamic, as every other one does (an object's
/// identity is observable); a function value has no identity (design principles 5.2), so binding
/// it changes nothing but which calls are known.
bool specializeDirectFuncInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t funcNodeId,
    bool functionValuesOnly = false);
/// Rewrites a CALL into a direct FUNC when its callee is known: a static Function (its static
/// captures bound into a specialization), or a closure built in this graph by FILL, whose graph
/// is lifted so that the captures become trailing norm arguments. With `unwrapGatedCallee` a
/// callee hidden behind gates (sync code gates a callee after the previous statement) is seen
/// through; the FUNC keeps the gate as a control input, so ordering is preserved.
bool devirtualizeStaticCallInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t callNodeId,
    bool unwrapGatedCallee = false);

/// The node whose value `id` reads when `id` projects an element out of a tuple built by FILL
/// in the same graph (through any chain of such projections); `id` itself otherwise. A tuple
/// that is written in place (see draft_writes.h) is not seen through: the projection may read
/// what a later assignment stored.
gc_node_ref_t resolveTupleProjection(const GraphDraft &draft, gc_node_ref_t id);

/// Specializes `draft`, a copy of `function`'s graph, for one call: its captured values become
/// constants and its norm ports take `normTypes` (entries may be null: the port keeps its type),
/// and the node types are re-inferred from them. `closureTypes`, when given, has one entry per
/// captured value: the type that constant gets instead of the one the closure declares (a type
/// the caller knows to be more precise for that value; null keeps the declared one).
void bindFunctionCall(
    GraphTransaction &transaction, const ::Function *function,
    std::span<camel::core::type::Type *const> normTypes,
    std::span<camel::core::type::Type *const> closureTypes = {});

} // namespace camel::runtime
