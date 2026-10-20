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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Lowering a LOOP node to the tail recursion it means (design principles 3.10: "first lower
 * LOOP to a tail-recursive subgraph at each subsystem's entry"; docs/design/graph-model 3.5, the
 * end of the LOOP card; docs/design/op-semantics 11.4).
 *
 * For a loop with state S (one value, or the k-tuple of k values), captures c and, for `for`,
 * the array xs, the LOOP node becomes a direct call of a new graph
 *
 *     rec(i, S; c, xs) = if go then next(S; c, i, xs) else done(S)
 *         go           = cond(S; c)                    While
 *                      = i < len(xs)                   For
 *                      = cond(S; c) & (i < len(xs))    ForWhile (the condition first, as the
 *                                                      native loop: camel/core/loop_kind.h)
 *     next(S; c, i, xs) = rec(i + 1, body(xs[i], S; c); c, xs)
 *     done(S)           = S
 *
 * (the index exists only with `for`; with k > 1 the condition and the body take the k values
 * of S, ACCS of the tuple, and the call passes the tuple of the first values). `next` and `done`
 * are the arms of the branch, each a graph called under the BRCH as the front end builds the
 * arms of an `if`; the call of `rec` in `next` is in tail position, so every back end runs it in
 * constant stack and it is not counted against the recursion limit: the shape is that of a
 * hand-written tail-recursive loop. The condition and body graphs are called unchanged.
 *
 * Privacy (op-semantics 11.4). The lowering is for a back end that does not run LOOP natively:
 * it returns a new, detached program (RuntimeGraphDraftSession::commit(true)) and leaves the
 * context's program as it is, so the program that passes, Tiles and the debugger see keeps its
 * structured loops. No pass of the standard pipeline calls it.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

namespace camel::runtime {

/// True when a LOOP node is reachable from `root`.
bool programHasLoops(GCGraph *root);

/// `program` with every LOOP node replaced by a call of a tail-recursive graph (see the file
/// comment): a detached copy, or `program` itself when it has no loop.
const Program *lowerLoopsToTailRecursion(
    const camel::core::context::context_ptr_t &context, const Program *program);

/// The same for the graphs reachable from `root`, a graph of `program`: the copy is rooted at
/// the copy of `root`.
const Program *lowerLoopsToTailRecursion(
    const camel::core::context::context_ptr_t &context, const Program *program, GCGraph *root);

} // namespace camel::runtime
