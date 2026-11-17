// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT
#pragma once

/*
 * The roots a region has, and which region decides whether a node runs (docs/design-principles.cn.md
 * 3.1, 3.3; docs/design/effects/analysis.cn.md 3.4, `regionRoot`): the nodes a running region runs
 * although no value of theirs is read.
 *
 * The two halves of 3.1 are answered by two mechanisms, and the author's spec of 2026-11-17
 * (`3e4c0929c`) moves only one of them:
 *
 *   * "a write reaches its region's exit" -- an edge: `Builder::layRegionWriteEdges`
 *     (src/compile/gir/builder_diagnostics.cpp) splices each write into its region's chain. That is
 *     the order of a write inside the region, and the walks below follow it like any other sync
 *     edge.
 *
 *   * "this region runs, and this statement belongs to it" -- a root: the question this file
 *     answers. It is *not* an order question and no edge can carry it. The sink an edge to a
 *     region's exit would need lies outside the region by construction: an arm's "exit" is the
 *     value it hands to the enclosing region (`builder_control.cpp#setArmResult` makes one in the
 *     enclosing region when the arm has no value), and the body's exit is the graph's exit, past
 *     every JOIN. An edge laid to either crosses a branch's cut, which the branch-arm invariant
 *     refuses (`src/runtime/branch_arms.cpp`) -- measured on the tree of `aa5c7d883`, 34 reds, two
 *     messages: `... runs between BRCH N and its JOIN M and belongs to no arm` and
 *     `... reads OPER n (state:set), which lies in an arm of BRCH N`. The same commit's own e9
 *     reading says it in one line: an edge orders, it does not start.
 *
 * So a write a running region owes is seeded as a *root* of its region by the four walks that ask
 * what runs (the warning, the liveness of a graph, the encode and the execution order); they ask
 * this file for the candidates, so the four cannot drift. The rule is read from the resource a
 * write names and never from the operator's name (3.3): a write of a resource outside the program,
 * or of a handle (a `Ref` the program owns, whose reader may stand in a graph this one cannot see).
 *
 * A `Variable` is not one of them (the ruling of 2026-11-15: assigning to a `var` is not an effect;
 * what it writes is a value handed in and handed back where the call stands), and neither is an
 * `Object`. A branch or a loop is not one either: what a branch's arms write are those nodes' own
 * writes, and "the branch runs" is a statement of its region, seeded below when it joins no value
 * (`branchJoinsNoValue`).
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

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace camel::runtime {

/// A node of a graph that makes a write a running region owes, and the region whose running decides
/// whether it runs (`decidingRegionOf`).
struct RegionRootCandidate {
    gc_node_ref_t node = kInvalidNodeRef;
    uint32_t region    = 0;

    bool operator==(const RegionRootCandidate &) const = default;
};

/// Whether running a node of `kind` whose operator is `oper` makes a write the region that runs it
/// owes. Read from the operator's declaration and never from the operator's name (design principles
/// 3.3): an access list with a write of a resource that reaches outside the program **or of a
/// handle** (a `Ref` the program owns: its reader may be in a graph this one cannot see), or the
/// conservative reading of an operator that itemized none -- "写全局资源", which is a write. A
/// `var` is not one of them: assigning to it is not an effect (the ruling of 2026-11-15). A node
/// that is not an operator writes nothing by itself: what a call does is its callee's, and a branch
/// or a loop is its regions'.
bool writesForTheRegion(GCNodeKind kind, camel::core::OperId oper);

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

/// Every operator node of `nodeCount` slots that makes a write its region owes, with the region that
/// decides whether it runs. `kindOf` answers `GCNodeKind::Dead` for a slot no node occupies.
template <typename KindOf, typename RegionOfNode, typename OperOf>
std::vector<RegionRootCandidate> candidatesWith(
    gc_cnt_t nodeCount, const KindOf &kindOf, const RegionOfNode &regionOfNode,
    const OperOf &operOf) {
    std::vector<RegionRootCandidate> candidates;
    for (gc_node_ref_t ref = 0; ref < nodeCount; ++ref) {
        const GCNodeKind kind = kindOf(ref);
        if (kind == GCNodeKind::Dead) {
            continue; // no node in this slot (a draft slot a rewrite emptied)
        }
        // Only an operator writes by itself (`writesForTheRegion` answers the same), and the test
        // is here as well so that the operator of a node that has none is never read.
        if (kind != GCNodeKind::Oper) {
            continue;
        }
        if (!writesForTheRegion(kind, operOf(ref))) {
            continue;
        }
        candidates.push_back(RegionRootCandidate{ref, regionOfNode(ref)});
    }
    return candidates;
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

/// Every node of `graph` that makes a write its region owes, in node order, each with the region
/// that decides whether it runs. The nodes of the body are the ones with `region == 0`.
std::vector<RegionRootCandidate> regionRootCandidatesOf(const GCGraph &graph);

/// The same questions answered by a draft still being built: `DraftLike` is the type a builder
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

template <typename DraftLike>
std::vector<RegionRootCandidate> regionRootCandidatesOfDraft(const DraftLike &draft) {
    return detail::candidatesWith(
        static_cast<gc_cnt_t>(draft.nodeSlotCount()),
        [&draft](gc_node_ref_t ref) -> GCNodeKind {
            if (!draft.alive(ref)) {
                return GCNodeKind::Dead; // a slot a rewrite emptied
            }
            const DraftNodeHeader *header = draft.header(ref);
            return header != nullptr ? header->kind : GCNodeKind::Dead;
        },
        [&draft](gc_node_ref_t ref) { return decidingRegionOfDraft(draft, ref); },
        [&draft](gc_node_ref_t ref) {
            if (!draft.alive(ref)) {
                return camel::core::kNoOper;
            }
            const std::span<const std::byte> payload = draft.payloadOf(ref);
            if (payload.size() < sizeof(GCOperBody)) {
                return camel::core::kNoOper;
            }
            return reinterpret_cast<const GCOperBody *>(payload.data())->oper;
        });
}

} // namespace camel::runtime