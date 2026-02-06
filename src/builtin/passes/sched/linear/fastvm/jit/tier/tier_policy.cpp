/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 06, 2026
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "tier_policy.h"

namespace camel::jit {

bool TierPolicy::shouldJit(GraphIR::Graph *graph, size_t pc) const {
    if (config_.policy == JitPolicy::Disabled)
        return false;
    if (config_.policy == JitPolicy::Always)
        return true;

    auto it = callCounts_.find(graph);
    if (it == callCounts_.end())
        return false;
    auto jt = it->second.find(pc);
    return jt != it->second.end() && jt->second >= config_.hotThreshold;
}

void TierPolicy::recordCall(GraphIR::Graph *graph, size_t pc) {
    if (config_.policy != JitPolicy::OnDemand)
        return;
    callCounts_[graph][pc]++;
}

} // namespace camel::jit
