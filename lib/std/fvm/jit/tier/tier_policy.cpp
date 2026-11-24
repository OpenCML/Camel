// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
