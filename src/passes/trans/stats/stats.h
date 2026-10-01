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
 * Created: Sep. 30, 2026
 * Updated: Sep. 30, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * std::stats: graph size statistics of the program as it is at that point of
 * the pass pipeline. Prints one line
 *   GRAPH_STATS {"graphs":..,"nodes":..,"edges":..,"opers":..,"direct_calls":..,
 *                "indirect_calls":..,"closures":..,"branches":..}
 * over every graph reachable from the root, and passes the program on
 * unchanged, so `camel f.cml std::macro std::stats std::opt::simplify std::stats`
 * compares a program before and after simplification.
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "camel/runtime/graph.h"

#include <string>

struct GraphStats {
    size_t graphs        = 0;
    size_t nodes         = 0;
    size_t edges         = 0;
    size_t opers         = 0;
    size_t directCalls   = 0;
    size_t indirectCalls = 0;
    size_t closures      = 0;
    size_t branches      = 0;

    std::string toJson() const;
};

/// Statistics of every graph reachable from `root`.
GraphStats collectGraphStats(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *root);

class GraphStatsPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
