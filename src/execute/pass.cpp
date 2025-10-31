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
 * Updated: Oct. 31, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "pass.h"
#include "error/diagnostics/diagnostics.h"

#include "builtin/passes/rewrite/inline/inline.h"
#include "builtin/passes/rewrite/macro/macro.h"
#include "builtin/passes/sched/linear/fastvm/fastvm.h"
#include "builtin/passes/sched/linear/nodevm/nodevm.h"
#include "builtin/passes/sched/parallel/taskflow/taskflow.h"
#include "builtin/passes/trans/bytecode/bytecode.h"
#include "builtin/passes/trans/dot/graphviz.h"
#include "builtin/passes/trans/tns/topo_node_seq.h"

using namespace GraphIR;

graph_ptr_t NullGraphIRPass::apply(graph_ptr_t &graph, std::ostream &os) {
    // Do nothing
    return Graph::null();
}

std::unordered_map<
    std::string, std::function<std::unique_ptr<GraphIRPass>(const context_ptr_t &ctx)>>
    passRegistry = {
        // Rewrite Passes
        {
            "std::inline",
            [](const context_ptr_t &ctx) { return std::make_unique<InlineRewritePass>(ctx); },
        },
        {
            "std::macro",
            [](const context_ptr_t &ctx) { return std::make_unique<MacroRewritePass>(ctx); },
        },

        // Schedule Passes
        {
            "std::linear",
            [](const context_ptr_t &ctx) { return std::make_unique<FastVMSchedPass>(ctx); },
        },
        {
            "std::fastvm",
            [](const context_ptr_t &ctx) { return std::make_unique<FastVMSchedPass>(ctx); },
        },
        {
            "std::nodevm",
            [](const context_ptr_t &ctx) { return std::make_unique<NodeVMSchedPass>(ctx); },
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
            [](const context_ptr_t &ctx) { return std::make_unique<FastVMSchedPass>(ctx); },
        },
        {
            "std::null",
            [](const context_ptr_t &ctx) { return std::make_unique<NullGraphIRPass>(ctx); },
        },

        // Translate Passes
        {
            "std::dot",
            [](const context_ptr_t &ctx) { return std::make_unique<GraphVizDumpPass>(ctx); },
        },
        {
            "std::graphviz",
            [](const context_ptr_t &ctx) { return std::make_unique<GraphVizDumpPass>(ctx); },
        },
        {
            "std::bytecode",
            [](const context_ptr_t &ctx) { return std::make_unique<BytecodeDumpPass>(ctx); },
        },
        {
            "std::tns",
            [](const context_ptr_t &ctx) { return std::make_unique<TopoNodeSeqDumpPass>(ctx); },
        },
};

int applyPasses(
    const std::vector<std::string> &passes, const context_ptr_t &ctx, std::ostream &os) {
    GraphIR::graph_ptr_t graph = ctx->rootGraph();

    for (const auto &p : passes) {
        ASSERT(graph != nullptr, "Graph is null.");
        ASSERT(
            !graph->dirty(),
            std::format("Graph {} is dirty, please rearrange it first.", graph->name()));

        if (graph == Graph::null()) {
            return 0;
        }

        auto it = passRegistry.find(p);
        if (it != passRegistry.end()) {
            auto pass = it->second(ctx);
            graph = pass->apply(graph, os);
            if (ctx->rtmDiags()->hasErrors()) {
                return 1;
            }
        } else {
            throw DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit(p);
        }
    }

    if (graph != Graph::null()) {
        ASSERT(
            !graph->dirty(),
            std::format("Graph {} is dirty, please rearrange it first.", graph->name()));

        if (passes.empty()) {
            // 如果用户没有指定任何遍，则默认使用 NodeVMSchedPass 进行调度
            auto fallback = std::make_unique<NodeVMSchedPass>(ctx);
            fallback->apply(graph, os);
        } else {
            // 如果用户指定了遍，则会将没有处理完的图直接丢弃
            auto fallback = std::make_unique<NullGraphIRPass>(ctx);
            fallback->apply(graph, os);
        }
    }

    return 0;
}
