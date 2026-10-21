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
 * Author: Camel Contributors
 * Created: Oct. 06, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
/// control predecessors. False (and no splice) when the callee cannot be inlined.
camel::runtime::DraftInlineResult inlineCallInOrder(
    camel::runtime::RuntimeGraphDraftSession &session, camel::runtime::GraphDraft &draft,
    camel::runtime::gc_node_ref_t funcNodeId);

/// Inlines the call of every arm of every branch of `program` (the front end builds each arm as
/// a graph called after the BRCH), so the arms are regions of the graphs that hold the branches.
/// The program itself when there is none.
/// With `detached`, the result is a private program of the graphs reachable from `root` (the
/// program of the context is left as it is).
const camel::runtime::Program *lowerBranchArms(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *root = nullptr, bool detached = false);

} // namespace camel::passes::opt
