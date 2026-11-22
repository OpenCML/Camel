// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The control regions of a graph (the arms of its branches, the parts of its loops) as a tree, with
 * the questions the validator and the back ends ask about it answered in constant time: which
 * control region a node is in, whether one region lies in another, which region is an arm of which
 * BRCH. Regions that only annotate (Inline, Block) are looked through.
 *
 * This reads a `GraphView` (kernel/graph/graph_view.h), so it answers a frozen graph and a draft
 * alike: the two entry points below are the two representations, and both build the adapter that
 * the view is (`FrozenGraphAdapter`, `DraftGraphAdapter`). One implementation of the tree, two ways
 * to hand it a graph; the tree is built from the view's region records and from nothing else.
 */

#pragma once

#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"
#include "graph/draft_view.h"
#include "graph/gc_graph_view.h"
#include "graph/graph_view.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

class ControlRegions {
  public:
    /// The tree of a frozen graph, and of the draft a builder holds. Both keep the adapter they
    /// build: the tree answers spans and ids of that graph, so the adapter has to outlive it (a
    /// temporary adapter would leave a view of a dead object).
    explicit ControlRegions(const GCGraph &graph)
        : owned_(std::make_unique<camel::graph::FrozenGraphAdapter>(graph)), view_(*owned_) {
        build();
    }
    explicit ControlRegions(const GraphDraft &draft)
        : owned_(std::make_unique<camel::graph::DraftGraphAdapter>(draft)), view_(*owned_) {
        build();
    }
    /// The tree of a graph already read through a view: the caller keeps the adapter alive.
    explicit ControlRegions(const camel::graph::GraphView &view) : view_(view) { build(); }

    /// Some branch or loop of the graph has a region.
    bool any() const { return !armsOf_.empty() || !loopsOf_.empty(); }
    /// The innermost control region around `node`; 0 when it is in none.
    uint32_t controlOf(gc_node_ref_t node) const {
        const uint32_t r = view_.regionOf(node);
        return r == 0 || r >= enclosing_.size() ? 0 : enclosing_[r];
    }
    /// The control region around the control region `control`; 0 for none.
    uint32_t parentControl(uint32_t control) const {
        const uint32_t parent = view_.region(control).parent;
        return parent == 0 || parent >= enclosing_.size() ? 0 : enclosing_[parent];
    }
    /// `inner` is `outer` or lies in it (0 is the body: everything lies in it).
    bool within(uint32_t inner, uint32_t outer) const {
        if (outer == 0) {
            return true;
        }
        return inner != 0 && tin_[outer] <= tin_[inner] && tin_[inner] <= tout_[outer];
    }
    /// The position of control region `control` in a walk of the tree that visits a region before
    /// the regions in it; a region and everything in it are the positions [tin, last].
    uint32_t tin(uint32_t control) const { return tin_[control]; }
    uint32_t last(uint32_t control) const { return tout_[control]; }
    /// The number of control regions plus the body, which is position 0.
    size_t size() const { return order_.size(); }
    /// The control region at walk position `position`.
    uint32_t regionAt(uint32_t position) const { return order_[position]; }
    /// The region of every arm of `brch`, by arm index (0 where an arm has none).
    std::span<const uint32_t> armsOf(gc_node_ref_t brch) const {
        const auto it = armsOf_.find(brch);
        return it == armsOf_.end() ? std::span<const uint32_t>{}
                                   : std::span<const uint32_t>(it->second);
    }
    /// The LoopCond (index 0) and LoopBody (index 1) regions of the LOOP node `loop`; 0 where it has
    /// none.
    std::array<uint32_t, 2> loopPartsOf(gc_node_ref_t loop) const {
        const auto it = loopsOf_.find(loop);
        return it == loopsOf_.end() ? std::array<uint32_t, 2>{0, 0} : it->second;
    }
    /// The arm of `brch` that the control region `control` lies in, -1 for none.
    int64_t armOf(uint32_t control, gc_node_ref_t brch) const {
        if (control == 0) {
            return -1;
        }
        const auto arms = armsOf(brch);
        for (size_t k = 0; k < arms.size(); ++k) {
            if (arms[k] != 0 && within(control, arms[k])) {
                return static_cast<int64_t>(k);
            }
        }
        return -1;
    }
    /// `node` runs in an arm of `brch` (in one of its arms, or deeper).
    bool inBranch(gc_node_ref_t node, gc_node_ref_t brch) const {
        return armOf(controlOf(node), brch) >= 0;
    }
    const camel::graph::GraphView &graph() const { return view_; }

  private:
    static bool isControl(camel::graph::RegionKind kind) {
        return kind == camel::graph::RegionKind::Arm ||
               kind == camel::graph::RegionKind::LoopCond ||
               kind == camel::graph::RegionKind::LoopBody;
    }

    void build();

    // Declared before the view it holds: the adapter is what the view reads, so it is built first
    // and destroyed last (`view_` binds into it).
    std::unique_ptr<camel::graph::GraphView> owned_;
    const camel::graph::GraphView &view_;
    std::vector<uint32_t> enclosing_; // by region: the innermost control region it is in (or is)
    std::vector<uint32_t> tin_, tout_; // by control region id; the body is 0
    std::vector<uint32_t> order_;      // control regions by walk position; the body first
    std::unordered_map<gc_node_ref_t, std::vector<uint32_t>> armsOf_;
    std::unordered_map<gc_node_ref_t, std::array<uint32_t, 2>> loopsOf_;
};

} // namespace camel::runtime