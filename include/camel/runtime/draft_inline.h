/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 10, 2026
 * Updated: Oct. 03, 2026
 * Supported by: National Key Research and Development Program of China
 */

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

DraftInlineResult inlineCallableInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t funcNodeId);
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
/// through; the FUNC keeps the gate as a control input, so ordering is preserved. That mode is
/// export-only: gate it on OptimizeRewriteConfig::enableBranchedCallSplit like the branched
/// split, since it exists for graphs that are only walked as value DAGs.
bool devirtualizeStaticCallInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t callNodeId,
    bool unwrapGatedCallee = false);

/// Rewrites a CALL whose callee is projected out of a branch JOIN (the value_and_grad pullback
/// shape: each arm packs (value, closure) into a tuple, the JOIN selects, and the closure is
/// called indirectly) into one CALL per arm plus one JOIN per used projection: the call's
/// result projections and the JOIN's other projections each select among the per-arm values.
/// Returns false (changing nothing) unless every arm's callee is statically devirtualizable
/// and the placement inside the branch arms is provably safe. EXPORT-ONLY: the sibling JOINs
/// this creates violate the scheduler's join-is-a-cut invariant, so it runs only under
/// OptimizeRewriteConfig::enableBranchedCallSplit.
bool devirtualizeBranchedCallInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t callNodeId);

/// The node whose value `id` reads when `id` projects an element out of a tuple built by FILL
/// in the same graph (through any chain of such projections); `id` itself otherwise. A tuple
/// that is written in place (see draft_writes.h) is not seen through: the projection may read
/// what a later assignment stored.
gc_node_ref_t resolveTupleProjection(const GraphDraft &draft, gc_node_ref_t id);

/// Specializes `draft`, a copy of `function`'s graph, for one call: its captured values become
/// constants and its norm ports take `normTypes` (entries may be null: the port keeps its type),
/// and the node types are re-inferred from them.
void bindFunctionCall(
    GraphDraft &draft, const ::Function *function,
    std::span<camel::core::type::Type *const> normTypes);

} // namespace camel::runtime
