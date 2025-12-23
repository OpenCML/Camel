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
 * Updated: Dec. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "pass.h"
#include "error/diagnostics/diagnostics.h"

#include "builtin/passes/rewrite/inline/inline.h"
#include "builtin/passes/sched/linear/fastvm/bcdump.h"
#include "builtin/passes/sched/linear/fastvm/fastvm.h"
#include "builtin/passes/sched/linear/nodevm/nodevm.h"
#include "builtin/passes/sched/parallel/taskflow/taskflow.h"
#include "builtin/passes/trans/dot/graphviz.h"
#include "builtin/passes/trans/tns/topo_node_seq.h"

#include "macro/macro.h"

using namespace GraphIR;

graph_ptr_t NullGraphIRPass::apply(graph_ptr_t &graph, std::ostream &os) {
    // Do nothing
    return Graph::null();
}

using PassFactory = std::function<std::unique_ptr<GraphIRPass>(const context_ptr_t &ctx)>;

std::unordered_map<std::string, PassFactory> passRegistry = {
    {
        "std::null",
        [](const context_ptr_t &ctx) { return std::make_unique<NullGraphIRPass>(ctx); },
    },
    {
        "std::macro",
        [](const context_ptr_t &ctx) { return std::make_unique<MacroRewritePass>(ctx); },
    },
    {
        "std::graphviz",
        [](const context_ptr_t &ctx) { return std::make_unique<GraphVizDumpPass>(ctx); },
    },
    {
        "std::topo_node_seq",
        [](const context_ptr_t &ctx) { return std::make_unique<TopoNodeSeqDumpPass>(ctx); },
    },
    {
        "std::nodevm",
        [](const context_ptr_t &ctx) { return std::make_unique<NodeVMSchedPass>(ctx); },
    },
    {
        "std::fastvm",
        [](const context_ptr_t &ctx) { return std::make_unique<FastVMSchedPass>(ctx); },
    },
    {
        "std::inline",
        [](const context_ptr_t &ctx) { return std::make_unique<InlineRewritePass>(ctx); },
    },
    {
        "std::taskflow",
        [](const context_ptr_t &ctx) { return std::make_unique<TaskflowExecSchedPass>(ctx); },
    },
    {
        "std::bytecode",
        [](const context_ptr_t &ctx) { return std::make_unique<BytecodeDumpPass>(ctx); },
    },
    {
        "std::linked_bytecode",
        [](const context_ptr_t &ctx) { return std::make_unique<LinkedBytecodeDumpPass>(ctx); },
    },
};

std::unordered_map<std::string, std::string> passAliases = {
    // 标准调度器
    {"std::fallback", "std::fastvm"},
    {"std::linear", "std::fastvm"},
    {"std::parallel", "std::taskflow"},

    // 常用vm缩写
    {"std::lnr", "std::fastvm"},
    {"std::prl", "std::taskflow"},
    {"std::fvm", "std::fastvm"},
    {"std::nvm", "std::nodevm"},
    {"std::svm", "std::stackvm"},
    {"std::tf", "std::taskflow"},

    // 常用转译遍缩写
    {"std::dot", "std::graphviz"},
    {"std::gir", "std::graphviz"},
    {"std::tns", "std::topo_node_seq"},
    {"std::bc", "std::bytecode"},
    {"std::lbc", "std::linked_bytecode"},
};

PassFactory findPassFactory(const std::string &name, std::ostream &os) {
    std::string stdName = name;
    auto aliasIt        = passAliases.find(name);
    if (aliasIt != passAliases.end()) {
        stdName = aliasIt->second;
    }

    auto regIt = passRegistry.find(stdName);
    if (regIt != passRegistry.end()) {
        return regIt->second;
    }

    os << std::format("Pass <{}> not found, available passes are:\n", name);
    for (const auto &[alias, pass] : passAliases) {
        os << std::format("  {} -> {}\n", alias, pass);
    }
    os << std::endl;

    return nullptr;
}

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

        auto factory = findPassFactory(p, os);
        if (factory) {
            auto pass = factory(ctx);
            graph     = pass->apply(graph, os);
            if (ctx->rtmDiags()->hasErrors()) {
                return 1;
            }
        } else {
            throw DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit(p);
        }
    }

    if (graph != Graph::null()) {
        auto factory = findPassFactory("std::fallback", os);
        if (factory) {
            auto pass = factory(ctx);
            graph     = pass->apply(graph, os);
            if (ctx->rtmDiags()->hasErrors()) {
                return 1;
            }
        } else {
            throw DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit("std::default");
        }
    }

    return 0;
}
