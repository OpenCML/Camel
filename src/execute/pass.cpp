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
 * Created: Oct. 21, 2024
 * Updated: Oct. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "pass.h"

#include "builtin/passes/sched/linear/dump/graphviz.h"
#include "builtin/passes/sched/linear/dump/topo_node_seq.h"
#include "builtin/passes/sched/linear/exec/fallback.h"
#include "builtin/passes/sched/parallel/exec/taskflow.h"

using namespace GraphIR;

graph_ptr_t NullGraphIRPass::apply(graph_ptr_t &graph, std::ostream &os) {
    // Do nothing
    return Graph::null();
}

std::unordered_map<
    std::string, std::function<std::unique_ptr<GraphIRPass>(const context_ptr_t &ctx)>>
    passRegistry = {
        {
            "std::dot",
            [](const context_ptr_t &ctx) { return std::make_unique<GraphVizDumpPass>(ctx); },
        },
        {
            "std::graphviz",
            [](const context_ptr_t &ctx) { return std::make_unique<GraphVizDumpPass>(ctx); },
        },
        {
            "std::tns",
            [](const context_ptr_t &ctx) { return std::make_unique<TopoNodeSeqDumpPass>(ctx); },
        },
        {
            "std::linear",
            [](const context_ptr_t &ctx) { return std::make_unique<FallbackExecSchedPass>(ctx); },
        },
        {
            "std::parallel",
            [](const context_ptr_t &ctx) { return std::make_unique<TaskflowExecSchedPass>(ctx); },
        },
        {
            "std::taskflow",
            [](const context_ptr_t &ctx) { return std::make_unique<TaskflowExecSchedPass>(ctx); },
        },
        {
            "std::default",
            [](const context_ptr_t &ctx) { return std::make_unique<FallbackExecSchedPass>(ctx); },
        },
        {
            "std::null",
            [](const context_ptr_t &ctx) { return std::make_unique<NullGraphIRPass>(ctx); },
        },
};

int applyPasses(
    const std::vector<std::string> &passes, const context_ptr_t &ctx, std::ostream &os) {
    GraphIR::graph_ptr_t entry = ctx->rootGraph();

    for (const auto &p : passes) {
        if (entry == Graph::null()) {
            return 0;
        }
        auto it = passRegistry.find(p);
        if (it != passRegistry.end()) {
            auto pass = it->second(ctx);
            entry = pass->apply(entry, os);
            if (ctx->rtmDiags()->hasErrors()) {
                return 1;
            }
        } else {
            throw CamelBaseException("Unknown pass: " + p);
        }
    }

    if (entry != Graph::null()) {
        auto fallback = std::make_unique<FallbackExecSchedPass>(ctx);
        fallback->apply(entry, os);
    }

    return 0;
}
