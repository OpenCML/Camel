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
 * Updated: Nov. 01, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "precompile.h"
#include "builtin/algo/topo.h"

using namespace std;
using namespace GraphIR;

const std::unordered_map<std::string, OpCode> &getSupportedInlineOperatorsMap() {
    static const std::unordered_map<std::string, OpCode> supportedInlineOperators = {
        {":op/add_i", OpCode::IADD}, {":op/add_l", OpCode::LADD},
        {":op/add_f", OpCode::FADD}, {":op/add_d", OpCode::DADD},

        {":op/sub_i", OpCode::ISUB}, {":op/sub_l", OpCode::LSUB},
        {":op/sub_f", OpCode::FSUB}, {":op/sub_d", OpCode::DSUB},

        {":op/mul_i", OpCode::IMUL}, {":op/mul_l", OpCode::LMUL},
        {":op/mul_f", OpCode::FMUL}, {":op/mul_d", OpCode::DMUL},

        {":op/div_i", OpCode::IDIV}, {":op/div_l", OpCode::LDIV},
        {":op/div_f", OpCode::FDIV}, {":op/div_d", OpCode::DDIV},

        {":op/lt_i", OpCode::ILT},   {":op/lt_l", OpCode::LLT},
        {":op/lt_f", OpCode::FLT},   {":op/lt_d", OpCode::DLT},

        {":op/gt_i", OpCode::IGT},   {":op/gt_l", OpCode::LGT},
        {":op/gt_f", OpCode::FGT},   {":op/gt_d", OpCode::DGT},

        {":op/eq_i", OpCode::IEQ},   {":op/eq_l", OpCode::LEQ},
        {":op/eq_f", OpCode::FEQ},   {":op/eq_d", OpCode::DEQ},

        {":op/ne_i", OpCode::INE},   {":op/ne_l", OpCode::LNE},
        {":op/ne_f", OpCode::FNE},   {":op/ne_d", OpCode::DNE},

        {":op/le_i", OpCode::ILE},   {":op/le_l", OpCode::LLE},
        {":op/le_f", OpCode::FLE},   {":op/le_d", OpCode::DLE},

        {":op/ge_i", OpCode::IGE},   {":op/ge_l", OpCode::LGE},
        {":op/ge_f", OpCode::FGE},   {":op/ge_d", OpCode::DGE},
    };
    return supportedInlineOperators;
}

shared_ptr<bytecode_vec_t>
precompile(const context_ptr_t &ctx, Graph *graph, const OptimizationStrategy &opt) {
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
            // 注意，这里除了要求 JOIN 节点是执行序列的最后一个节点之外
            // 还必须保证 JOIN 节点的返回值就是 Graph 的返回值
            // 如果 Graph 选择返回的值不是 JOIN 节点的产生值，不能做尾调用优化
            // 因为尾调用优化之后，原 Frame 就会被释放，这样 Graph 无法返回正确的值
            bool isTail = i == topoSortedNodes.size() - 1 && graph->outputNode() == node;

            if (joinTargetMap.find(node.get()) != joinTargetMap.end()) {
                for (const auto &[jumpIdx, fromIdx] : joinTargetMap[node.get()]) {
                    auto &jump = (*bytecodes)[jumpIdx];
                    auto &from = (*bytecodes)[fromIdx];
                    jump.fastop[0] = as_index(bytecodes->size());
                    if (opt.enableTailCallDetection && isTail && from.opcode == OpCode::FUNC) {
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
            bool isTail = i == topoSortedNodes.size() - 1 && graph->outputNode() == node;
            auto funcNode = tt::as_shared<FuncNode>(node);
            appendBytecode(
                *bytecodes,
                (opt.enableTailCallDetection && isTail) ? OpCode::TAIL : OpCode::FUNC,
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
            const auto &uri = opNode->oper()->uri();

            // 尝试内联算子
            if (opt.enableInlineOperators) {
                const auto &inlineOpMap = getSupportedInlineOperatorsMap();
                auto it = inlineOpMap.find(uri);
                if (it != inlineOpMap.end()) {
                    appendBytecode(
                        *bytecodes,
                        it->second,
                        node->index(),
                        {
                            normOps.front(),
                            normOps.back(),
                        });
                    break;
                }
            }

            if (uri.starts_with(":mark/")) {
                MarkOpCode markOp;
                if (uri == ":mark/map_arr") {
                    markOp = MarkOpCode::MapArr;
                } else if (uri == ":mark/apply_arr") {
                    markOp = MarkOpCode::ApplyArr;
                } else if (uri == ":mark/reduce_arr") {
                    markOp = MarkOpCode::ReduceArr;
                } else if (uri == ":mark/filter_arr") {
                    markOp = MarkOpCode::FilterArr;
                } else if (uri == ":mark/foreach_arr") {
                    markOp = MarkOpCode::ForeachArr;
                } else {
                    ctx->rtmDiags()->of(RuntimeDiag::UnrecognizedOperatorURI).commit(uri);
                    break;
                }

                appendBytecode(
                    *bytecodes,
                    OpCode::SCHD,
                    node->index(),
                    {},
                    normOps,
                    withOps,
                    true,
                    {
                        .mark = markOp,
                    });
                break;
            }

            const auto opFunc = ctx->execMgr().find(uri);

            if (!opFunc) {
                ctx->rtmDiags()->of(RuntimeDiag::UnrecognizedOperatorURI).commit(uri);
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
        case NodeType::NREF: {
            // 一般无操作
            // 但在内联函数中，有时候会直连JOIN
            // 这时候需要插入一个JUMP节点而不能直接跳过
            // 否则会导致该条路径为空，进而导致跳转错误
            break; // break 而不能是 continue
        }

        case NodeType::PORT:
            [[fallthrough]];
        case NodeType::EXIT:
            [[fallthrough]];
        case NodeType::SYNC:
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
