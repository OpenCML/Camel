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
 * Author: Zhenjie Wei
 * Created: Apr. 10, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../inline/config.h"
#include "camel/execute/pass/opt.h"

class OptimizeRewritePass : public RuntimeGraphRewritePass {
  public:
    OptimizeRewritePass(
        const camel::core::context::context_ptr_t &ctx, const OptimizeRewriteConfig &config = {})
        : RuntimeGraphRewritePass(ctx), config_(config) {}
    ~OptimizeRewritePass() override = default;

    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;

  private:
    OptimizeRewriteConfig config_{};
};
