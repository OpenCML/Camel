// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <camel/runtime/graph.h>

#include <string>
#include <vector>

namespace camel::runtime {

/**
 * The four references a graph's numbering can break, checked on one frozen graph:
 *
 * - `BRCH.join` is `kInvalidNodeRef` or a node of this graph that is a JOIN;
 * - `JOIN.brch` is `kInvalidNodeRef` or a node of this graph that is a BRCH;
 * - a BRCH and its JOIN name each other (a `Brch(j).join == k` says `Join(k).brch == j`);
 * - a region's `owner` and `result` are `kInvalidNodeRef` or nodes of this graph.
 *
 * One function with two callers, because a graph is judged twice in its life and both judgments
 * must be the same judgment: `EdgeChecker` runs it on a frozen graph, and `encodeDraftGraph` runs
 * it on the graph it is about to publish. A draft whose numbers do not resolve is refused where it
 * would be written rather than where a later reader trips over them.
 *
 * Each violation is one sentence naming the graph, the node or region, and the reference that does
 * not resolve. An empty result means every one of the four holds.
 *
 * **What this can and cannot catch today, measured rather than assumed** (2026-11-02):
 *
 * - The encoder's half of this check fires zero times over the whole plan tree: no program in the
 *   suite builds a draft with a reference that does not resolve. It is a refusal, not a report, and
 *   there is no case for it -- see the next point for why one cannot be written from a pass.
 * - A corrupted draft cannot be built with the draft API: `GraphDraft` has setters for a region's
 *   result and owner and none for `BRCH.join` / `JOIN.brch`. Writing the branch payload through
 *   `DraftNodeView::payloadAs` compiles and is not sound -- the same write (join = 9999) produced
 *   "its join is node 0, which is not a join" on one run and "its join is 24469 and the graph has 8
 *   nodes" on the next, because the encoder rebuilds branch numbering from the draft's arms rather
 *   than from the payload field that was written. A case built on that would be flaky, which is
 *   worse than absent.
 * - `setRegionResult(id, 9999)` is not refused either: the encoded graph runs, exit code 0, because
 *   the number was remapped to a node of the graph on the way in.
 *
 * So the region half of these four checks is a range test that the encoder cannot be made to fail,
 * and the branch half needs a setter that does not exist. If a case is wanted, the draft API needs
 * a typed `setBrchJoin` first -- not another raw payload write.
 */
std::vector<std::string> checkGraphCrossReferences(const GCGraph &graph);

} // namespace camel::runtime