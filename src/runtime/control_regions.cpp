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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/runtime/control_regions.h"

namespace camel::runtime {

ControlRegions::ControlRegions(const GCGraph &graph) : graph_(graph) {
    const uint32_t count = static_cast<uint32_t>(graph.regionCount());
    enclosing_.assign(count, 0);
    tin_.assign(count == 0 ? 1 : count, 0);
    tout_.assign(count == 0 ? 1 : count, 0);
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
            if (isControl(graph.region(at)->kind)) {
                break;
            }
            at = graph.region(at)->parent;
        }
        uint32_t found = 0;
        if (at != 0 && at < count) {
            found = done[at] ? enclosing_[at] : (isControl(graph.region(at)->kind) ? at : 0);
        }
        for (const uint32_t id : chain) {
            enclosing_[id] = isControl(graph.region(id)->kind) ? id : found;
            done[id]       = 1;
        }
    }
    // The tree of the control regions and a walk of it.
    std::vector<std::vector<uint32_t>> children(count);
    for (uint32_t r = 1; r < count; ++r) {
        const GCRegion &region = *graph.region(r);
        if (!isControl(region.kind)) {
            continue;
        }
        children[parentControl(r)].push_back(r);
        if (region.kind == GCRegionKind::Arm) {
            auto &arms = armsOf_[region.owner];
            if (arms.size() <= region.index) {
                arms.resize(region.index + 1, 0);
            }
            arms[region.index] = r;
        } else {
            auto &parts = loopsOf_.try_emplace(region.owner, std::array<uint32_t, 2>{0, 0})
                              .first->second;
            parts[region.kind == GCRegionKind::LoopCond ? 0 : 1] = r;
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
