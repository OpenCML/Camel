// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Inlining one call the way std::opt::inline does it.
 *
 * The splice itself (camel/runtime/draft_inline.h) copies the callee's body in place of the
 * call. A call also ordered the code around it: it ran after everything its arguments were
 * computed from, and its users ran after it. A body that does not read every argument, or
 * that has effects of its own, loses that order unless it is made explicit around the splice.
 * This is the one place that does so; std::opt::inline, std::opt and any transformation that has
 * to bring a callee's body into a graph (autodiff, to follow a function value) go through it.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/runtime/draft_inline.h"

namespace camel::passes::opt {

/// Inlines the direct call `funcNodeId` (a FUNC node) of `draft`, keeping the order the program
/// gave its statements: the inlined body, when it may have an effect, waits for the ordered
/// sources of all the call's arguments, and the call's users wait for them and for the call's
/// control predecessors. False (and no splice) when the callee cannot be inlined. See
/// inlineCallableInDraft for `asArm`.
camel::runtime::DraftInlineResult inlineCallInOrder(
    camel::runtime::RuntimeGraphDraftSession &session, camel::runtime::GraphDraft &draft,
    camel::runtime::gc_node_ref_t funcNodeId, bool asArm = false);

} // namespace camel::passes::opt
