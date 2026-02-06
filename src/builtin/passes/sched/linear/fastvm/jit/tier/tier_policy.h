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

#pragma once

#include "builtin/passes/sched/linear/fastvm/jit/jit_config.h"

#include "compile/gir.h"

#include <cstddef>
#include <unordered_map>

namespace camel::jit {

class TierPolicy {
  public:
    explicit TierPolicy(const JitConfig &config) : config_(config) {}

    bool shouldJit(GraphIR::Graph *graph, size_t pc) const;
    void recordCall(GraphIR::Graph *graph, size_t pc);

  private:
    JitConfig config_;
    std::unordered_map<GraphIR::Graph *, std::unordered_map<size_t, size_t>> callCounts_;
};

} // namespace camel::jit
