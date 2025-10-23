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
 * Updated: Oct. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "precompile.h"
#include "builtin/algo/topo.h"

using namespace std;
using namespace GraphIR;

shared_ptr<bytecode_vec_t> precompile(const context_ptr_t &ctx, Graph *graph) {
    // 从图的出口节点开始反向拓扑排序（逆序 DFS）
    node_ptr_t exitNode = graph->exitNode();

    auto topoSortedNodes = findReachable(
        exitNode,
        [](const node_ptr_t &node) {
            vector<node_ptr_t> inputs;
            inputs.reserve(node->dataInputs().size() + node->ctrlInputs().size());

            // 控制输入优先（用于控制流）
            for (const auto &in : node->ctrlInputs()) {
                if (&in->graph() == &node->graph())
                    inputs.push_back(in);
            }

            // 数据输入后添加（用于计算依赖）
            for (const auto &in : node->dataInputs()) {
                if (&in->graph() == &node->graph())
                    inputs.push_back(in);
            }

            return inputs;
        },
        true // 跳过起始节点本身
    );

    // Debug 模式下打印拓扑排序结果并检查不可达节点
    EXEC_WHEN_DEBUG([&]() {
        l.in("Topo").debug("Topologically sorted nodes for graph {}:", graph->name());
        for (const auto &node : topoSortedNodes) {
            l.in("Topo").debug("  {}", node->toString());
        }

        size_t totalNodeCount =
            graph->nodes().size() + graph->ports().size() + graph->closure().size();
        if (topoSortedNodes.size() != totalNodeCount) {
            GraphIR::node_vec_t unreachableNodes;
            for (const auto &node : graph->nodes()) {
                if (node != exitNode &&
                    std::find(topoSortedNodes.begin(), topoSortedNodes.end(), node) ==
                        topoSortedNodes.end()) {
                    unreachableNodes.push_back(node);
                }
            }

            std::string unreachableInfo;
            for (const auto &node : unreachableNodes) {
                if (!unreachableInfo.empty())
                    unreachableInfo += ", ";
                unreachableInfo += node->toString();
            }

            l.in("Topo").warn(
                "Unreachable nodes in graph {} detected: {}",
                graph->name(),
                unreachableInfo);
        }
    }());

    auto bytecodes = make_shared<bytecode_vec_t>();
    bytecodes->reserve(topoSortedNodes.size() * 1.2); // 预估容量

    // 用于回填跳转地址的映射表
    std::unordered_map<GraphIR::Node *, size_t> brchTargetMap;
    std::unordered_map<GraphIR::Node *, std::vector<size_t>> joinTargetMap;

    for (const auto &node : topoSortedNodes) {
        ASSERT(node->index() < 256, "Node index exceeds bytecode limit.");
        ASSERT(node->dataInDegree() <= 20, "Too many inputs for bytecode instruction.");

        // 回填之前记录的 JUMP 跳转地址
        if (brchTargetMap.find(node.get()) != brchTargetMap.end()) {
            size_t jumpIndex = brchTargetMap[node.get()];
            (*bytecodes)[jumpIndex].extra.target = bytecodes->size();
            brchTargetMap.erase(node.get());
        }

        // 创建并初始化字节码指令
        Bytecode bytecode{
            .opcode = OpCode::COPY, // 默认操作码，稍后根据节点类型修改
            .self = static_cast<uint8_t>(node->index()),
            .withCnt = static_cast<uint8_t>(node->withInputs().size()),
            .normCnt = static_cast<uint8_t>(node->normInputs().size()),
            .inputs = {},
            .extra = {},
        };

        size_t inputIdx = 0;
        for (const auto &in : node->withInputs()) {
            ASSERT(in->index() < 256, "Input node index out of range.");
            bytecode.inputs[inputIdx++] = static_cast<uint8_t>(in->index());
        }
        for (const auto &in : node->normInputs()) {
            ASSERT(in->index() < 256, "Input node index out of range.");
            bytecode.inputs[inputIdx++] = static_cast<uint8_t>(in->index());
        }

        // 根据节点类型设置操作码和额外信息
        switch (node->type()) {
        case NodeType::CAST:
            ASSERT(false, "CAST node not implemented.");
            break;

        case NodeType::COPY:
            bytecode.opcode = OpCode::COPY;
            break;

        case NodeType::FILL:
            bytecode.opcode = OpCode::FILL;
            break;

        case NodeType::ACCS: {
            bytecode.opcode = OpCode::ACCS;
            auto accNode = tt::as_shared<AccsNode>(node);
            if (accNode->isNum()) {
                bytecode.extra.index = accNode->index<size_t>();
            } else {
                ASSERT(false, "ACCS node with non-numeric index not supported.");
            }
            break;
        }

        case NodeType::BRCH: {
            bytecode.opcode = OpCode::BRCH;

            bytecodes->push_back(bytecode);

            // 为这个 BRCH 节点的每个控制输出添加一个占位跳转指令
            for (const auto &ctrlOut : node->ctrlOutputs()) {
                // 记录跳转目标在 bytecode 中的位置
                brchTargetMap[ctrlOut.get()] = bytecodes->size();
                bytecodes->push_back(Bytecode{
                    // 占位跳转指令，目标地址稍后回填
                    .opcode = OpCode::JUMP,
                    .self = 255, // 占位，无实际节点对应
                    .withCnt = 0,
                    .normCnt = 0,
                    .inputs = {},
                    .extra = {},
                });
            }

            continue;
        }

        case NodeType::JOIN: {
            bytecode.opcode = OpCode::JOIN;

            if (joinTargetMap.find(node.get()) != joinTargetMap.end()) {
                for (const auto &jumpPos : joinTargetMap[node.get()]) {
                    (*bytecodes)[jumpPos].extra.target = bytecodes->size();
                }
                joinTargetMap.erase(node.get());
            }

            break;
        }

        case NodeType::CALL:
            bytecode.opcode = OpCode::CALL;
            break;

        case NodeType::BIND:
            ASSERT(false, "BIND node not implemented.");
            break;

        case NodeType::FUNC: {
            bytecode.opcode = OpCode::FUNC;
            auto funcNode = tt::as_shared<FuncNode>(node);
            bytecode.extra.graph = &(funcNode->func()->graph());
            break;
        }

        case NodeType::OPER: {
            bytecode.opcode = OpCode::OPER;
            auto opNode = tt::as_shared<OperNode>(node);
            const auto opFunc = ctx->execMgr().find(opNode->oper()->uri());

            if (!opFunc) {
                ctx->rtmDiags()
                    ->of(RuntimeDiag::UnrecognizedOperatorURI)
                    .commit(opNode->oper()->uri());
            }

            bytecode.extra.func = *opFunc;
            break;
        }

        default:
            continue;
        }

        bytecodes->push_back(bytecode);

        // 如果该节点的输出连接到 JOIN 节点，则插入一个跳转到 JOIN 的 JUMP
        if (node->withOutputs().size() == 1 &&
            node->withOutputs().front()->type() == NodeType::JOIN) {
            auto joinNode = node->withOutputs().front();
            joinTargetMap[joinNode.get()].push_back(bytecodes->size());
            bytecodes->push_back(Bytecode{
                // 占位跳转指令，目标地址稍后回填
                .opcode = OpCode::JUMP,
                .self = 255, // 占位，无实际节点对应
                .withCnt = 0,
                .normCnt = 0,
                .inputs = {},
                .extra = {},
            });
        }
    }

    ASSERT(
        brchTargetMap.empty(),
        "Some BRCH nodes have unmatched control outputs without corresponding JOIN nodes.");
    ASSERT(
        joinTargetMap.empty(),
        "Some JOIN nodes have unmatched JUMP instructions without corresponding targets.");

    return bytecodes;
}
