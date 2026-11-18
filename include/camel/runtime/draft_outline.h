// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Runtime draft outline helpers.
 *
 * `inline` splices a function value's body into a region; this is its reverse: a group of nodes of
 * a draft becomes a graph of its own, and a call of it takes the place of the value the group handed
 * on. The graph the group becomes is the one the transaction *made* -- it was in no program before
 * the edit -- so it is delivered by the commit through `RuntimeGraphDraftSession::noteMadeGraph`,
 * which is what `docs/design/graph-editing/README.cn.md` 2.5 asks a commit to deliver beside the
 * graphs the edited function reaches.
 */

#pragma once

#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"

#include <string_view>
#include <vector>

namespace camel::runtime {

struct DraftOutlineResult {
    /// The graph the group became: a graph the session made, which every commit of the session
    /// delivers (`noteMadeGraph`).
    GCGraph *graph = nullptr;
    /// The call of it that stands where the group's value stood, in the draft the group came from.
    gc_node_ref_t call = kInvalidNodeRef;
    /// The values the group read from outside, in the order they became the new graph's ports.
    std::vector<gc_node_ref_t> entries;

    explicit operator bool() const { return graph != nullptr; }
};

/// The boundary a group of nodes has: the values it reads from outside, in the order a port is made
/// of each, and the one value it hands on. Throws `std::runtime_error` naming the rule the group
/// breaks when it is not a segment with such a boundary -- an empty group, a node the transaction
/// does not hold, a node that anchors its graph, a node that carries an access or an ordering, no
/// value out or more than one.
struct DraftOutlineBoundary {
    std::vector<gc_node_ref_t> entries;
    gc_node_ref_t result = kInvalidNodeRef;
};
DraftOutlineBoundary draftOutlineBoundary(
    const GraphDraft &draft, std::span<const gc_node_ref_t> group);

/// Makes the group a graph of its own in `session`'s program and puts a call of it where the group's
/// value stood, so that the copy the group's nodes became is reached through that call (the call is
/// what a pass reads the new function with, and what `inline` expands again).
///
/// The new graph's signature is read from the group: one ordinary parameter per entry, in the order
/// `draftOutlineBoundary` gives them, and the result's type as the exit type. The group's nodes are
/// copied, not moved: nothing a group does can be copied without its type, its operator and what
/// reads it, and the copy is the graph's body while the group's own nodes are erased once nothing in
/// the draft reads them any more.
///
/// `name` is the name the graph is created with, and the name it is mangled from.
DraftOutlineResult outlineDraftNodes(
    RuntimeGraphDraftSession &session, GraphTransaction &transaction,
    std::span<const gc_node_ref_t> group, std::string_view name);

} // namespace camel::runtime