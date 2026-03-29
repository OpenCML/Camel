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
 * Created: Oct. 25, 2025
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/execute/pass/opt.h"

enum class InlineTargetStrategy {
    Small,
    Arm,
    Hybrid,
};

struct InlineRewriteConfig {
    InlineTargetStrategy strategy           = InlineTargetStrategy::Hybrid;
    size_t smallSubgraphMaxNonDataPortNodes = 8;
    bool blockCallsToSccEntryCallees        = true;
};

class InlineRewritePass : public GraphRewritePass {
  public:
    InlineRewritePass(
        const camel::core::context::context_ptr_t &ctx, const InlineRewriteConfig &config = {})
        : GraphRewritePass(ctx), config_(config) {};
    virtual ~InlineRewritePass() = default;

    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;

  private:
    InlineRewriteConfig config_{};
};
