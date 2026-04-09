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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "bcdump.h"

#include "camel/runtime/reachable.h"
#include "compile.h"

using namespace GIR;

graph_ptr_t BytecodeDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    std::vector<camel::runtime::GCGraph *> allGraphs =
        camel::runtime::collectReachableGraphs(graph);

    os << "[index] opcode (opsize) [self] | [fastops] | <with> (norm) | extra\n";

    for (auto *g : allGraphs) {
        auto bytecodes = compile(
            context_,
            g,
            {
                .enableTailCallDetection = true,
                .enableInlineOperators   = true,
                .optimizationStrategies  = OptimizationStrategyCode::All,
            });
        os << g->mangledName() << ":\n";

        int maxwidth = computeWidth(bytecodes.size());

        for (size_t i = 0; i < bytecodes.size();) {
            os << "  [" << formatIndex(i, maxwidth) << "] ";
            os << opCodeToString(bytecodes[i], context_) << "\n";
            i += bytecodes[i].opsize;
        }

        os << std::format("  [used: {}, allocated: {}]\n", bytecodes.size(), bytecodes.capacity());
    }

    return GIR::Graph::null();
}

graph_ptr_t LinkedBytecodeDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    auto linked = compileAndLink(
        context_,
        graph,
        {
            .enableTailCallDetection = true,
            .enableInlineOperators   = true,
            .optimizationStrategies  = OptimizationStrategyCode::All,
        });

    os << "[index] opcode (opsize) [self] | [fastops] | <with> (norm) | extra\n";

    int maxwidth = computeWidth(linked.codes.size());

    for (const auto &[offset, length, runtimeGraph] : linked.graphs) {
        ASSERT(runtimeGraph != nullptr, "Linked bytecode entry is missing runtime graph metadata.");
        os << runtimeGraph->mangledName() << ":\n";
        for (size_t i = offset; i < offset + length;) {
            os << "  [" << formatIndex(i, maxwidth) << "] ";
            os << opCodeToString(linked.codes[i], context_) << "\n";
            i += linked.codes[i].opsize;
        }
    }

    return GIR::Graph::null();
}
