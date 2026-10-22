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
/// Bound on the type-only specializations of one recursive function that a program may hold.
///
/// A specialization that only refines parameter types (a tensor argument whose type carries its
/// shape) re-infers the callee's types, so the recursive call inside the copy can carry yet
/// another type: a recursion whose tensor grows every level (`loop(i + 1, n, concat(acc, row))`)
/// would get a new copy per level without end, because nothing in the types says where the
/// recursion stops. A recursion that keeps its types reuses its one copy and never meets the
/// bound; past the bound the call keeps its current callee, which accepts the argument as it is.
inline constexpr size_t kMaxTypeSpecializationsPerRecursiveFunction = 8;

bool specializeDirectFuncInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t funcNodeId);
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
    GraphDraft &draft, const ::Function *function,
    std::span<camel::core::type::Type *const> normTypes,
    std::span<camel::core::type::Type *const> closureTypes = {});

} // namespace camel::runtime
