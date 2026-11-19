// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/*
 * Which region decides whether a node runs (design principles 3.1, 3.7; the region table of
 * camel/runtime/graph.h): the innermost arm or loop region the node sits in, and the BRCH or LOOP
 * whose running is that region's running.
 *
 * This file used to hold the *roots* a region has -- the nodes a running region runs although no
 * value of theirs is read, seeded by four walks (the warning, the liveness of a graph, the encode
 * and the execution order) and re-derived by each. The author's spec of 2026-11-17 (`3e4c0929c`,
 * design principles 3.1) replaced that derivation with an edge: "建图时，区域里每个按效应推断会写
 * 外部资源或句柄的节点（算子、调用、分支、循环一样）连一条同步边到本区域的出口，不论它的值有没有
 * 人读；... 执行、清理与各后端只沿边走，不另行推导哪些必须执行". So the use is laid once, by
 * `Builder::layRegionWriteEdges` (src/compile/gir/builder_diagnostics.cpp), from the node to the
 * exit of the region that decides it -- `GCRegion::result`, the node whose value the region yields
 * -- and every walk follows edges from then on.
 *
 * What is left here is the question that edge needs answered: which region decides whether a node
 * runs, and which node decides whether a region runs. A node of the body is decided by the body
 * (region 0, which runs whenever the graph does); a node in an arm or a loop body is decided by the
 * innermost such region around it, and that region by its owner. An inlined body and a tagged block
 * are transparent -- the program cannot choose to run them or not -- so the walk goes through them.
 *
 * Two shapes ask the question: a frozen `GCGraph` and the editable draft a builder holds. The draft
 * side is a template over the shape (`decidingRegionOfDraft`), not an overload on the draft's type,
 * so this header and its unit stay independent of it: the graph-store ledger
 * (test/cases/runtime/graph_draft_ledger) counts every file wired to the draft, and a utility that
 * only reads three accessors is not one of them.
 */

#include "camel/core/operator_table.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"

#include <cstdint>

namespace camel::runtime {

namespace detail {

/// The kinds of region the program chooses to run or not. Everything else -- the body, an inlined
/// body, a tagged block, a region a rewrite removed -- is transparent: a node in it runs with the
/// region around it, which is why `decideOf` walks past it instead of stopping there.
inline bool isDeciding(GCRegionKind kind) {
    switch (kind) {
    case GCRegionKind::Arm:
    case GCRegionKind::LoopCond:
    case GCRegionKind::LoopBody:
        return true;
    case GCRegionKind::Root:
    case GCRegionKind::Inline:
    case GCRegionKind::Block:
    case GCRegionKind::Dead:
        break;
    }
    return false;
}

/// The region whose running decides whether the node in region `at` runs, walking outwards.
template <typename RegionAt>
uint32_t decideOf(uint32_t regionCount, const RegionAt &regionAt, uint32_t at) {
    // The ids of a graph are not reused and a region's parent is always a region the graph holds
    // (0 is every region's ancestor), so the walk ends: at the body, or at a region of a kind the
    // program does not choose.
    for (uint32_t depth = 0; depth <= regionCount; ++depth) {
        if (at == 0) {
            return 0;
        }
        const GCRegion *one = regionAt(at);
        if (one == nullptr) {
            return 0; // a region the table does not hold is not a place the program chose
        }
        if (isDeciding(one->kind)) {
            return at;
        }
        if (one->parent == at) {
            return 0; // a region that is its own parent: the walk would not end
        }
        at = one->parent;
    }
    return 0;
}

} // namespace detail

/// The region whose running decides whether `ref` runs: the innermost arm or loop region the node
/// sits in, and 0 -- the body -- for a node of the body and for one in a region the program cannot
/// choose to run or not.
uint32_t decidingRegionOf(const GCGraph &graph, gc_node_ref_t ref);

/// The node whose running is the running of region `region`: the BRCH or the LOOP that region is
/// a part of (GCRegion::owner), and kInvalidNodeRef for the body, which runs whenever the graph
/// does.
gc_node_ref_t decidingOwnerOf(const GCGraph &graph, uint32_t region);

/// The same two questions answered by a draft still being built: `DraftLike` is the type a builder
/// holds, and it must answer `regionCount()`, `nodeRegion(ref)`, `region(id)`, `alive`,
/// `header`, `payloadOf` and `nodeSlotCount`. The draft's `region` aborts on an id it does not
/// hold, so the parent id read out of a stale record is answered here instead: an id nobody holds
/// is a region the program did not choose (the walk's own null answer).
template <typename DraftLike>
uint32_t decidingRegionOfDraft(const DraftLike &draft, gc_node_ref_t ref) {
    return detail::decideOf(
        static_cast<uint32_t>(draft.regionCount()),
        [&draft](uint32_t id) -> const GCRegion * {
            return id < draft.regionCount() ? &draft.region(id) : nullptr;
        },
        draft.nodeRegion(ref));
}

template <typename DraftLike>
gc_node_ref_t decidingOwnerOfDraft(const DraftLike &draft, uint32_t region) {
    if (region == 0 || region >= draft.regionCount()) {
        return kInvalidNodeRef;
    }
    return draft.region(region).owner;
}

} // namespace camel::runtime
