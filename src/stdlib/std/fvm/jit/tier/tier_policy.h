// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "../jit_config.h"

#include <cstdint>

namespace camel::jit {

class TierPolicy {
  public:
    explicit TierPolicy(const JitConfig &config) : config_(config) {}

    /// Whether a graph is compiled now. `callCount` is what the call site has seen; `hasLoop`
    /// says the graph runs a loop of regions. Under the eager settings of the policy (threshold
    /// 0 or 1) such a graph is hot on its first call: what makes it expensive is the rounds,
    /// which no call count can see, and a graph entered once (the entry function of a program,
    /// above all) can run millions of them. An explicit threshold above 1 keeps the call-count
    /// rule, so a measurement can still ask for nothing to be compiled.
    bool shouldJit(uint32_t callCount, bool hasLoop = false) const;

  private:
    JitConfig config_;
};

} // namespace camel::jit
