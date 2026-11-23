// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The control regions of a graph (the arms of its branches, the parts of its loops), with the
 * questions the validator and the back ends ask about it answered in constant time: which control
 * region a node is in, whether one region lies in another, which region is an arm of which BRCH.
 * Regions that only annotate (Inline, Block) are looked through.
 *
 * The tree itself is the fact `core.controlRegions` (camel/facts/control_regions.h): one builder,
 * one tree per graph, and this class is the reading of it -- it computes nothing, it holds the tree
 * and the view the tree was made from. A reader that built its own tree would be a second answer to
 * the same question with its own lifetime, which is what this class exists to avoid.
 */

#pragma once

#include "camel/facts/control_regions.h"

#include "camel/runtime/graph.h"

#include "graph/graph_view.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace camel::runtime {

class ControlRegions {
  public:
    /// The tree of a frozen graph, which is a fact of it: built once, and the same tree for every
    /// reader of that graph. The view it reads through is the graph's own (`Graph::view()`), which
    /// the graph keeps for as long as it lives.
    explicit ControlRegions(const GCGraph &graph);

    /// Some branch or loop of the graph has a region.
    bool any() const { return !tree_->arms.empty() || !tree_->loops.empty(); }
    /// The innermost control region around `node`; 0 when it is in none.
    uint32_t controlOf(gc_node_ref_t node) const {
        const uint32_t r = view_->regionOf(node);
        return r == 0 || r >= tree_->enclosing.size() ? 0 : tree_->enclosing[r];
    }
    /// The control region around the control region `control`; 0 for none.
    uint32_t parentControl(uint32_t control) const {
        const uint32_t parent = view_->region(control).parent;
        return parent == 0 || parent >= tree_->enclosing.size() ? 0 : tree_->enclosing[parent];
    }
    /// `inner` is `outer` or lies in it (0 is the body: everything lies in it).
    bool within(uint32_t inner, uint32_t outer) const {
        if (outer == 0) {
            return true;
        }
        return inner != 0 && tree_->tin[outer] <= tree_->tin[inner] &&
               tree_->tin[inner] <= tree_->last[outer];
    }
    /// The position of control region `control` in a walk of the tree that visits a region before
    /// the regions in it; a region and everything in it are the positions [tin, last].
    uint32_t tin(uint32_t control) const { return tree_->tin[control]; }
    uint32_t last(uint32_t control) const { return tree_->last[control]; }
    /// The number of control regions plus the body, which is position 0.
    size_t size() const { return tree_->order.size(); }
    /// The control region at walk position `position`.
    uint32_t regionAt(uint32_t position) const { return tree_->order[position]; }
    /// The region of every arm of `brch`, by arm index (0 where an arm has none).
    std::span<const uint32_t> armsOf(gc_node_ref_t brch) const {
        const auto it = tree_->arms.find(brch);
        return it == tree_->arms.end() ? std::span<const uint32_t>{}
                                       : std::span<const uint32_t>(it->second);
    }
    /// The LoopCond (index 0) and LoopBody (index 1) regions of the LOOP node `loop`; 0 where it has
    /// none.
    std::array<uint32_t, 2> loopPartsOf(gc_node_ref_t loop) const {
        const auto it = tree_->loops.find(loop);
        return it == tree_->loops.end() ? std::array<uint32_t, 2>{0, 0} : it->second;
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
    const camel::graph::GraphView &graph() const { return *view_; }

  private:
    // The view is the graph's own and lives as long as it does; the tree is the fact, shared with
    // every other reader of this graph.
    const camel::graph::GraphView *view_;
    std::shared_ptr<const camel::facts::ControlRegionTree> tree_;
};

} // namespace camel::runtime