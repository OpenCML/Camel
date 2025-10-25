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
 * Updated: Oct. 25, 2025
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
            node_vec_t unreachableNodes;
            for (const auto &node : graph->nodes()) {
                if (node != exitNode &&
                    find(topoSortedNodes.begin(), topoSortedNodes.end(), node) ==
                        topoSortedNodes.end()) {
                    unreachableNodes.push_back(node);
                }
            }

            string unreachableInfo;
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
    bytecodes->reserve(topoSortedNodes.size() * 3); // 预估容量

    // 用于回填跳转地址的映射表
    unordered_map<Node *, size_t> brchTargetMap;
    // JOIN*, FROM*
    unordered_map<Node *, vector<pair<size_t, size_t>>> joinTargetMap;

    for (size_t i = 0; i < topoSortedNodes.size(); ++i) {
        auto &node = topoSortedNodes[i];

        size_t currIdx = bytecodes->size();

        // 回填之前记录的 JUMP 跳转地址
        if (brchTargetMap.find(node.get()) != brchTargetMap.end()) {
            size_t jumpIndex = brchTargetMap[node.get()];
            auto &header = (*bytecodes)[jumpIndex];
            header.fastop[0] = as_index(bytecodes->size());
            brchTargetMap.erase(node.get());
        }

        vector<data_idx_t> normOps, withOps;
        for (const auto &in : node->normInputs()) {
            normOps.push_back(in->index());
        }
        for (const auto &in : node->withInputs()) {
            withOps.push_back(in->index());
        }

        // 根据节点类型设置操作码和额外信息
        switch (node->type()) {
        case NodeType::CAST:
            ASSERT(false, "CAST node not implemented.");
            break;

        case NodeType::COPY:
            appendBytecode(
                *bytecodes,
                OpCode::COPY,
                node->index(),
                {node->normInputs().front()->index()});
            break;

        case NodeType::FILL:
            appendBytecode(*bytecodes, OpCode::FILL, node->index(), {}, normOps, withOps);
            break;

        case NodeType::ACCS: {
            auto accNode = tt::as_shared<AccsNode>(node);
            ASSERT(accNode->isNum(), "Only numeric ACCS indices are supported in bytecode.");
            appendBytecode(
                *bytecodes,
                OpCode::ACCS,
                node->index(),
                {
                    node->normInputs().front()->index(),
                    as_index(accNode->index<size_t>()),
                });
            break;
        }

        case NodeType::BRCH: {
            appendBytecode(*bytecodes, OpCode::BRCH, node->index(), {}, normOps, withOps);

            // 为这个 BRCH 节点的每个控制输出添加一个占位跳转指令
            for (const auto &ctrlOut : node->ctrlOutputs()) {
                // 记录跳转目标
                brchTargetMap[ctrlOut.get()] = bytecodes->size();
                // 占位跳转指令，目标地址稍后回填
                appendBytecode(
                    *bytecodes,
                    OpCode::JUMP,
                    0, // 占位，无实际节点对应
                    {0});
            }

            continue;
        }

        case NodeType::JOIN: {
            bool isTail = i == topoSortedNodes.size() - 1;

            if (joinTargetMap.find(node.get()) != joinTargetMap.end()) {
                for (const auto &[jumpIdx, fromIdx] : joinTargetMap[node.get()]) {
                    auto &jump = (*bytecodes)[jumpIdx];
                    auto &from = (*bytecodes)[fromIdx];
                    jump.fastop[0] = as_index(bytecodes->size());
                    if (isTail && from.opcode == OpCode::FUNC) {
                        from.opcode = OpCode::TAIL;
                    }
                }
                joinTargetMap.erase(node.get());
            }

            appendBytecode(*bytecodes, OpCode::JOIN, node->index(), {}, normOps, withOps);

            break;
        }

        case NodeType::CALL:
            appendBytecode(*bytecodes, OpCode::CALL, node->index(), {}, normOps, withOps);
            break;

        case NodeType::BIND:
            ASSERT(false, "BIND node not implemented.");
            break;

        case NodeType::FUNC: {
            auto funcNode = tt::as_shared<FuncNode>(node);
            appendBytecode(
                *bytecodes,
                i == topoSortedNodes.size() - 1 ? OpCode::TAIL : OpCode::FUNC,
                node->index(),
                {},
                normOps,
                withOps,
                true,
                {
                    .graph = &(funcNode->func()->graph()),
                });
            break;
        }

        case NodeType::OPER: {
            auto opNode = tt::as_shared<OperNode>(node);
            const auto opFunc = ctx->execMgr().find(opNode->oper()->uri());

            if (!opFunc) {
                ctx->rtmDiags()
                    ->of(RuntimeDiag::UnrecognizedOperatorURI)
                    .commit(opNode->oper()->uri());
            }

            appendBytecode(
                *bytecodes,
                OpCode::OPER,
                node->index(),
                {},
                normOps,
                withOps,
                true,
                {
                    .func = *opFunc,
                });
            break;
        }

        case NodeType::DATA:
            [[fallthrough]];
        case NodeType::PORT:
            [[fallthrough]];
        case NodeType::EXIT:
            [[fallthrough]];
        case NodeType::DREF:
            // 这些节点类型不生成字节码
            continue;

        default:
            ASSERT(
                false,
                std::format(
                    "Unsupported node type encountered in bytecode generation: {}",
                    to_string(node->type())));
        }

        // 如果该节点的输出连接到 JOIN 节点，则插入一个跳转到 JOIN 的 JUMP
        if (node->withOutputs().size() == 1 &&
            node->withOutputs().front()->type() == NodeType::JOIN) {
            auto joinNode = node->withOutputs().front();
            joinTargetMap[joinNode.get()].push_back({
                bytecodes->size(),
                currIdx,
            });
            appendBytecode(*bytecodes, OpCode::JUMP, 0, {0});
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
