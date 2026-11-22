// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The control-region tree (camel/runtime/control_regions.h), built from the view's region records:
 * one build for both representations, because the tree is a reading of the region table and the view
 * answers that table the same way for a frozen graph and for a draft.
 */

#include "camel/runtime/control_regions.h"

namespace camel::runtime {

void ControlRegions::build() {
    const uint32_t count = view_.regionCount();
    enclosing_.assign(count, 0);
    tin_.assign(count, 0);
    tout_.assign(count, 0);
    order_.push_back(0);
    if (count == 0) {
        return;
    }
    // Parents have smaller ids than the regions in them only by accident of construction, so the
    // enclosing control region is resolved by walking up, once per region.
    std::vector<uint8_t> done(count, 0);
    std::vector<uint32_t> chain;
    for (uint32_t r = 1; r < count; ++r) {
        chain.clear();
        uint32_t at = r;
        while (at != 0 && at < count && !done[at]) {
            chain.push_back(at);
            if (isControl(view_.region(at).kind)) {
                break;
            }
            at = view_.region(at).parent;
        }
        uint32_t found = 0;
        if (at != 0 && at < count) {
            found = done[at] ? enclosing_[at] : (isControl(view_.region(at).kind) ? at : 0);
        }
        for (const uint32_t id : chain) {
            enclosing_[id] = isControl(view_.region(id).kind) ? id : found;
            done[id]       = 1;
        }
    }
    // The tree of the control regions and a walk of it.
    std::vector<std::vector<uint32_t>> children(count);
    for (uint32_t r = 1; r < count; ++r) {
        const camel::graph::RegionRecord region = view_.region(r);
        if (!isControl(region.kind)) {
            continue;
        }
        children[parentControl(r)].push_back(r);
        if (region.kind == camel::graph::RegionKind::Arm) {
            auto &arms = armsOf_[region.owner];
            if (arms.size() <= region.index) {
                arms.resize(region.index + 1, 0);
            }
            arms[region.index] = r;
        } else {
            auto &parts = loopsOf_.try_emplace(region.owner, std::array<uint32_t, 2>{0, 0})
                              .first->second;
            parts[region.kind == camel::graph::RegionKind::LoopCond ? 0 : 1] = r;
        }
    }
    std::vector<std::pair<uint32_t, size_t>> stack{{0, 0}};
    while (!stack.empty()) {
        auto &[region, next] = stack.back();
        if (next == 0 && region != 0) {
            tin_[region] = static_cast<uint32_t>(order_.size());
            order_.push_back(region);
        }
        if (next < children[region].size()) {
            const uint32_t child = children[region][next++];
            stack.push_back({child, 0});
            continue;
        }
        tout_[region] = static_cast<uint32_t>(order_.size() - 1);
        stack.pop_back();
    }
}

} // namespace camel::runtime