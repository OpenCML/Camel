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
 * Updated: Feb. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "builtin/passes/sched/linear/fastvm/jit/jit_config.h"

#include "compile/gir.h"

#include <cstdint>

namespace camel::jit {

class TierPolicy {
  public:
    explicit TierPolicy(const JitConfig &config) : config_(config) {}

    bool shouldJit(uint32_t callCount) const;

  private:
    JitConfig config_;
};

} // namespace camel::jit
