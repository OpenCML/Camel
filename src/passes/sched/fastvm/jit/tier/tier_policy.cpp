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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "tier_policy.h"

namespace camel::jit {

bool TierPolicy::shouldJit(uint32_t callCount, bool hasLoop) const {
    if (config_.policy == JitPolicy::Disabled)
        return false;
    if (config_.policy == JitPolicy::Always)
        return true;
    // A loop graph under the eager settings (threshold 0 or 1, the default): what makes it
    // expensive is the rounds, which a call count cannot see. A threshold above them is how a run
    // asks for cold code to stay interpreted (`hot_threshold=1000000000` is the "nothing
    // compiled" measurement of 07_jit_architecture.md), and it keeps its meaning.
    const bool eagerLoop = hasLoop && config_.hotThreshold <= 1;
    return eagerLoop || callCount > config_.hotThreshold;
}

} // namespace camel::jit
