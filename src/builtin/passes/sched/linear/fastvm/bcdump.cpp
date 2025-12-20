
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
 * Created: Oct. 21, 2025
 * Updated: Dec. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "bcdump.h"
#include "builtin/algo/topo.h"
#include "precompile.h"

using namespace GraphIR;

graph_ptr_t BytecodeDumpPass::apply(graph_ptr_t &graph, std::ostream &os) {
    // 收集所有被依赖的子图
    std::vector<graph_ptr_t> allGraphs;
    allGraphs.push_back(graph);
    for (const auto &[_, gSet] : graph->subGraphs()) {
        for (const auto &g : gSet) {
            auto sortedSubGraphs =
                findReachable(g, [](const graph_ptr_t &g) { return g->dependencies(); });
            allGraphs.insert(allGraphs.end(), sortedSubGraphs.begin(), sortedSubGraphs.end());
        }
    }

    std::unordered_set<graph_ptr_t> visited;

    os << "[index] opcode (opsize) [self] | [fastops] | <with> (norm) | extra\n";

    for (const auto &g : allGraphs) {
        if (visited.count(g))
            continue;
        visited.insert(g);

        auto bytecodes = compile(
            context_,
            g.get(),
            {
                .enableTailCallDetection = true,
                .enableInlineOperators   = true,
                .optimizationStrategies  = OptimizationStrategyCode::All,
            });
        os << g->mangledName() << ":\n";

        for (size_t i = 0; i < bytecodes.size();) {
            os << opCodeToString(bytecodes[i], i, context_) << "\n";
            i += bytecodes[i].opsize;
        }

        os << std::format("  [used: {}, allocated: {}]\n", bytecodes.size(), bytecodes.capacity());
    }

    return GraphIR::Graph::null();
}

graph_ptr_t LinkedBytecodeDumpPass::apply(graph_ptr_t &graph, std::ostream &os) {
    const auto &[codes, graphs, _] = compileAndLink(
        context_,
        graph.get(),
        {
            .enableTailCallDetection = true,
            .enableInlineOperators   = true,
            .optimizationStrategies  = OptimizationStrategyCode::All,
        });

    os << "[index] opcode (opsize) [self] | [fastops] | <with> (norm) | extra\n";

    for (const auto &[offset, length, graph] : graphs) {
        os << graph->mangledName() << ":\n";
        for (size_t i = offset; i < offset + length;) {
            os << opCodeToString(codes[i], i, context_) << "\n";
            i += codes[i].opsize;
        }
    }

    return GraphIR::Graph::null();
}
