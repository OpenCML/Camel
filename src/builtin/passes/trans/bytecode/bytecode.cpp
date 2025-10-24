
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
 * Updated: Oct. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "bytecode.h"
#include "builtin/algo/topo.h"
#include "builtin/passes/sched/common/precompile.h"

using namespace GraphIR;

graph_ptr_t BytecodeDumpPass::apply(graph_ptr_t &graph, std::ostream &os) {
    auto optMainGraph = graph->getSubGraphsByName("main");
    ASSERT(optMainGraph.has_value(), "Main graph not found.");
    auto mainGraphSet = optMainGraph.value();
    ASSERT(!mainGraphSet.empty(), "Main graph set is empty.");
    ASSERT(mainGraphSet.size() == 1, "Multiple main graphs found.");
    auto mainGraph = *mainGraphSet.begin();

    // 收集所有被依赖的子图
    auto sortedGraphs =
        findReachable(mainGraph, [](const graph_ptr_t &g) { return g->dependencies(); });

    for (const auto &g : sortedGraphs) {
        auto bytecodes = precompile(context_, g.get());
        os << g->mangledName() << ":\n";
        for (size_t i = 0; i < bytecodes->size();) {
            const auto &bc = (*bytecodes)[i];
            std::string operandStr;
            if (bc.hasOperands()) {
                size_t withCnt = bc.fastop[0];
                size_t normCnt = bc.fastop[1];
                operandStr = "<";
                for (size_t j = 0; j < withCnt; j++) {
                    operandStr += std::to_string(bc.operands()[j]);
                    if (j + 1 < withCnt)
                        operandStr += ", ";
                }
                operandStr += "> (";
                for (size_t j = 0; j < normCnt; j++) {
                    operandStr += std::to_string(bc.operands()[withCnt + j]);
                    if (j + 1 < normCnt)
                        operandStr += ", ";
                }
                operandStr += ")";
            } else {
                operandStr = "<> ()";
            }
            os << std::format(
                "  [{}] {} | {} | {}\n",
                formatIndex(i),
                bc.toString(),
                operandStr,
                bc.opcode == OpCode::OPER
                    ? context_->execMgr().getNameOfAnOperator(bc.extra()->func)
                    : bc.extra()->toString(bc.opcode));
            i += bc.opsize;
        }
        os << std::format(
            "  [used: {}, allocated: {}]\n",
            bytecodes->size(),
            bytecodes->capacity());
    }

    return graph;
}
