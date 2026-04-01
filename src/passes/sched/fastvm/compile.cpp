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
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "compile.h"
#include "camel/common/algo/topo.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/func.h"
#include "camel/execute/executor.h"

#include <algorithm>
#include <cstddef>

using namespace std;
using namespace GIR;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;

static Node *resolveTailValueNode(Graph *graph) {
    Node *outputNode = graph->outputNode();
    if (!outputNode) {
        return nullptr;
    }
    if (outputNode->type() != NodeType::GATE) {
        return outputNode;
    }
    // sync 场景下 EXIT 常被 GATE 包装：Norm 输入承载返回值，Ctrl 输入仅承载副作用顺序。
    // 尾调用判定应优先参考值路径，避免把控制路径误当作返回值来源。
    if (!outputNode->normInputs().empty()) {
        return outputNode->normInputs().back();
    }
    return nullptr;
}

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

bytecode_vec_t compile(
    const context_ptr_t &ctx, Graph *graph, const CompileStrategy &opt,
    std::unordered_map<size_t, camel::source::origin_id_t> *localPcOrigins) {
    // 从图的出口节点开始反向拓扑排序（逆序 DFS）
    Node *exitNode = graph->exitNode();

    auto topoSortedNodes = findReachable(
        exitNode,
        [](Node *node) {
            vector<Node *> inputs;
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
        false // 包含值出口锚点本身
    );

    // Debug 模式下打印拓扑排序结果并检查不可达节点
    EXEC_WHEN_DEBUG({
        CAMEL_LOG_DEBUG_S("Topo", "Topologically sorted nodes for graph {}:", graph->name());
        for (const auto &node : topoSortedNodes) {
            CAMEL_LOG_DEBUG_S("Topo", "  {}", node->toString());
        }

        size_t totalNodeCount =
            graph->nodes().size() + graph->ports().size() + graph->closure().size();
        auto contains = [](node_span_t nodes, Node *target) {
            return std::find(nodes.begin(), nodes.end(), target) != nodes.end();
        };
        const bool exitCounted =
            contains(graph->nodes(), exitNode) || contains(graph->normPorts(), exitNode) ||
            contains(graph->withPorts(), exitNode) || contains(graph->closure(), exitNode);
        const size_t expectedTopoCount = totalNodeCount + (exitCounted ? 0 : 1);
        if (topoSortedNodes.size() != expectedTopoCount) {
            node_vec_t unreachableNodes;
            for (Node *node : graph->nodes()) {
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

            CAMEL_LOG_WARN_S(
                "Topo",
                "Unreachable nodes in graph {} detected: {}",
                graph->name(),
                unreachableInfo);
        }
    });

    auto bytecodes = bytecode_vec_t();
    bytecodes.reserve(topoSortedNodes.size() * 3); // 预估容量
    std::unordered_set<Node *> topoNodeSet(topoSortedNodes.begin(), topoSortedNodes.end());
    Node *tailValueNode            = resolveTailValueNode(graph);
    auto hasOnlyTrivialSuffixAfter = [&](size_t index) {
        for (size_t j = index + 1; j < topoSortedNodes.size(); ++j) {
            Node *suffixNode = topoSortedNodes[j];
            if (suffixNode->type() != NodeType::GATE) {
                return false;
            }
        }
        return true;
    };

    // 用于回填跳转地址的映射表。
    //
    // 为什么同一个目标节点会被多个 BRCH arm 共享？
    // 1) 多个 arm 的控制流在重写/优化后直接收敛到同一控制头节点；
    // 2) arm 头节点不可达时会回退到 matched JOIN，多个 arm 可能回退到同一个 JOIN；
    // 3) BRCH/JOIN 的槽位语义允许不同 arm 槽位引用同一节点（按槽位区分语义，而非按指针唯一）。
    //
    // 因此目标 -> 占位索引必须是一对多映射，否则后写会覆盖先写，导致部分 JUMP 无法回填。
    unordered_map<Node *, vector<size_t>> brchTargetMap;
    // JOIN*, FROM*
    unordered_map<Node *, vector<pair<size_t, size_t>>> joinTargetMap;

    for (size_t i = 0; i < topoSortedNodes.size(); ++i) {
        auto &node = topoSortedNodes[i];

        size_t currIdx     = bytecodes.size();
        auto sourceContext = ctx ? ctx->sourceContext() : nullptr;
        auto nodeOrigin    = sourceContext ? sourceContext->resolveGirNodeOrigin(node)
                                           : camel::source::kInvalidOriginId;

        // 回填之前记录的 JUMP 跳转地址
        if (auto it = brchTargetMap.find(node); it != brchTargetMap.end()) {
            for (size_t jumpIndex : it->second) {
                auto &header     = bytecodes[jumpIndex];
                header.fastop[0] = as_index(bytecodes.size());
            }
            brchTargetMap.erase(it);
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
        case NodeType::CAST: {
            const auto &inputNode = node->normInputs().front();
            Type *targetType      = node->dataType();
            BytecodeExtra extra;
            extra.pType = targetType;
            appendBytecode(
                bytecodes,
                OpCode::CAST,
                node->index(),
                {inputNode->index()},
                {},
                {},
                true,
                extra);
            break;
        }

        case NodeType::COPY:
            appendBytecode(
                bytecodes,
                OpCode::COPY,
                node->index(),
                {node->normInputs().front()->index()});
            break;

        case NodeType::FILL:
            appendBytecode(bytecodes, OpCode::FILL, node->index(), {}, normOps, withOps);
            break;

        case NodeType::ACCS: {
            auto *accNode           = tt::as_ptr<AccsNode>(node);
            const auto &srcNode     = node->normInputs().front();
            const auto &srcDataType = srcNode->dataType();
            ASSERT(srcDataType->isComposite(), "ACCS source node must be composite.");

            size_t index = 0;

            switch (srcDataType->code()) {
            case TypeCode::Tuple: {
                ASSERT(accNode->isNum(), "ACCS index must be numeric.");
                index                 = accNode->numIndex();
                const auto &tupleType = tt::as_ptr<TupleType>(srcDataType);
                if (index >= tupleType->size()) {
                    ctx->rtmDiags()->of(SemanticDiag::InvalidAccessIndex).commit(to_string(index));
                    index = 0;
                }
                break;
            }
            case TypeCode::Struct: {
                ASSERT(!accNode->isNum(), "ACCS index must be string.");
                const auto *structType = tt::as_ptr<camel::core::type::StructType>(srcDataType);
                const auto &optIndex   = structType->findField(accNode->strIndex());
                if (!optIndex.has_value()) {
                    ctx->rtmDiags()
                        ->of(SemanticDiag::InvalidAccessIndex)
                        .commit(accNode->strIndex());
                }
                index = optIndex.value();
                break;
            }
            default:
                ASSERT(false, "Unsupported ACCS source node type.");
            }

            appendBytecode(
                bytecodes,
                OpCode::ACCS,
                node->index(),
                {
                    srcNode->index(),
                    as_index(index),
                });
            break;
        }

        case NodeType::BRCH: {
            auto *brch = tt::as_ptr<BrchNode>(node);
            appendBytecode(bytecodes, OpCode::BRCH, node->index(), {}, normOps, withOps);

            // BRCH 语义 arm 数与 JOIN 的 with 槽位一一对应：
            // - if/else: withCnt == 0, armCount == 2
            // - match:  armCount == withCnt + 1 (最后一个是 default/else)
            const size_t expectedArmCount = withOps.empty() ? 2 : withOps.size() + 1;
            Node *joinFallbackTarget      = nullptr;
            if (brch->hasMatchedJoin() && topoNodeSet.contains(brch->matchedJoin())) {
                joinFallbackTarget = brch->matchedJoin();
            }

            vector<Node *> armHeads;
            armHeads.reserve(expectedArmCount);

            // 优先使用 matched JOIN 的 ctrlInputs 顺序作为 arm 顺序来源，确保与 JOIN 槽位对齐。
            // 某些图形态下 JOIN 可能暂时不显式携带 ctrlInputs（例如历史图或部分重写中间态），
            // 此时回退到 BRCH.ctrlOutputs 顺序。
            if (brch->hasMatchedJoin() &&
                brch->matchedJoin()->ctrlInputs().size() == expectedArmCount) {
                auto *joinNode = brch->matchedJoin();
                auto joinCtrls = joinNode->ctrlInputs();
                for (size_t armIdx = 0; armIdx < expectedArmCount; ++armIdx) {
                    Node *head = joinCtrls[armIdx];
                    if (!topoNodeSet.contains(head)) {
                        ASSERT(
                            joinFallbackTarget != nullptr,
                            std::format(
                                "BRCH arm head is unreachable and no JOIN fallback exists in "
                                "compile for graph '{}': brch={}, arm={}, head={}.",
                                graph->name(),
                                brch->toString(),
                                armIdx,
                                head ? head->toString() : "<null>"));
                        head = joinFallbackTarget;
                    }
                    armHeads.push_back(head);
                }
            } else {
                // 兜底路径：无 matched JOIN 时回退到 BRCH 的 ctrlOutputs 顺序。
                auto ctrlOuts = brch->ctrlOutputs();
                ASSERT(
                    ctrlOuts.size() >= expectedArmCount,
                    std::format(
                        "BRCH ctrl output count is insufficient in compile for graph '{}': "
                        "brch={}, expectedArms={}, ctrlOuts={}.",
                        graph->name(),
                        brch->toString(),
                        expectedArmCount,
                        ctrlOuts.size()));
                for (size_t armIdx = 0; armIdx < expectedArmCount; ++armIdx) {
                    Node *head = ctrlOuts[armIdx];
                    if (!topoNodeSet.contains(head)) {
                        ASSERT(
                            joinFallbackTarget != nullptr,
                            std::format(
                                "BRCH ctrl output arm is unreachable and no JOIN fallback exists "
                                "in compile for graph '{}': brch={}, arm={}, head={}.",
                                graph->name(),
                                brch->toString(),
                                armIdx,
                                head ? head->toString() : "<null>"));
                        head = joinFallbackTarget;
                    }
                    armHeads.push_back(head);
                }
            }

            // 为每个语义 arm 生成固定一条 JUMP 占位，保证 BRCH jumpIdx 与 arm slot 严格一致。
            for (Node *armHead : armHeads) {
                brchTargetMap[armHead].push_back(bytecodes.size());
                appendBytecode(bytecodes, OpCode::JUMP, 0, {0});
            }
            if (localPcOrigins && nodeOrigin != camel::source::kInvalidOriginId) {
                (*localPcOrigins)[currIdx] = nodeOrigin;
            }

            continue;
        }

        case NodeType::JOIN: {
            // JOIN 尾调判定：
            // 1) JOIN 必须是图返回值路径上的最终值节点（tailValueNode）；
            // 2) 其后只允许存在无执行语义的 GATE 后缀节点。
            // 这样可覆盖 sync 包装图（output=GATE）下的尾调机会，同时避免越过真实可执行节点。
            bool isTail = node == tailValueNode && hasOnlyTrivialSuffixAfter(i);

            if (joinTargetMap.find(node) != joinTargetMap.end()) {
                for (const auto &[jumpIdx, fromIdx] : joinTargetMap[node]) {
                    auto &jump     = bytecodes[jumpIdx];
                    auto &from     = bytecodes[fromIdx];
                    jump.fastop[0] = as_index(bytecodes.size());
                    if (opt.enableTailCallDetection && isTail && from.opcode == OpCode::FUNC) {
                        from.opcode = OpCode::TAIL;
                    }
                }
                joinTargetMap.erase(node);
            }

            appendBytecode(bytecodes, OpCode::JOIN, node->index(), {}, normOps, withOps);

            break;
        }

        case NodeType::CALL:
            appendBytecode(bytecodes, OpCode::CALL, node->index(), {}, normOps, withOps);
            break;

        case NodeType::BIND:
            ASSERT(false, "BIND node not implemented.");
            break;

        case NodeType::FUNC: {
            bool isTail    = node == tailValueNode && hasOnlyTrivialSuffixAfter(i);
            auto *funcNode = tt::as_ptr<FuncNode>(node);
            // 将上下文参数合并到普通参数中，因为函数调用不区分参数类型
            // 这样还可以把 fastop[1] 留空，以便放其他内容
            normOps.insert(normOps.end(), withOps.begin(), withOps.end());
            appendBytecode(
                bytecodes,
                (opt.enableTailCallDetection && isTail) ? OpCode::TAIL : OpCode::FUNC,
                node->index(),
                {},
                normOps,
                {},
                true,
                {.graph = funcNode->bodyGraph()},
#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
                2
#else
                1
#endif
            );
            break;
        }

        case NodeType::OPER: {
            auto *opNode    = tt::as_ptr<OperNode>(node);
            const auto &uri = opNode->oper()->uri();

            // 尝试内联算子
            if (opt.enableInlineOperators) {
                const auto &inlineOpMap = getSupportedInlineOperatorsMap();
                auto it                 = inlineOpMap.find(uri);
                if (it != inlineOpMap.end()) {
                    appendBytecode(
                        bytecodes,
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
                    bytecodes,
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
                bytecodes,
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
        case NodeType::GATE: {
            // 一般无操作
            // 但在内联函数中，有时候会直连JOIN
            // 这时候需要插入一个JUMP节点而不能直接跳过
            // 否则会导致该条路径为空，进而导致跳转错误
            break; // break 而不能是 continue
        }

        case NodeType::PORT:
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

        if (localPcOrigins && nodeOrigin != camel::source::kInvalidOriginId) {
            (*localPcOrigins)[currIdx] = nodeOrigin;
        }

        // 如果该节点的输出连接到 JOIN 节点，则插入一个跳转到 JOIN 的 JUMP
        if (node->hasMatchedJoinOutput()) {
            auto *joinNode = node->matchedJoinOutput();
            joinTargetMap[joinNode].push_back({
                bytecodes.size(),
                currIdx,
            });
            appendBytecode(bytecodes, OpCode::JUMP, 0, {0});
        }
    }

    appendBytecode(bytecodes, OpCode::RETN, 0, {graph->exitNode()->index()});

    ASSERT(
        brchTargetMap.empty(),
        "Some BRCH nodes have unmatched control outputs without corresponding JOIN nodes.");
    ASSERT(
        joinTargetMap.empty(),
        "Some JOIN nodes have unmatched JUMP instructions without corresponding targets.");

    // 优化字节码
    BytecodeOptimizer optimizer(opt.optimizationStrategies);
    optimizer.optimize(bytecodes, 0, localPcOrigins);

    return bytecodes;
}

std::tuple<bytecode_vec_t, std::vector<BytecodeIndex>, std::unordered_map<GIR::Graph *, size_t>>
compileAndLink(context_ptr_t ctx, GIR::Graph *entry, const CompileStrategy &opt) {
    bytecode_vec_t linked;
    std::vector<BytecodeIndex> graphs;
    std::unordered_map<GIR::Graph *, size_t> offsetMap;

    // 收集 entry 通过 subGraphs / dependencies 任一关系可达的全部图。
    std::unordered_set<GIR::Graph *> visited;
    std::vector<GIR::Graph *> uniqueGraphs;
    std::function<void(const graph_ptr_t &)> collect = [&](const graph_ptr_t &curr) {
        if (!curr || !visited.insert(curr.get()).second) {
            return;
        }
        uniqueGraphs.push_back(curr.get());
        for (const auto &[_, gSet] : curr->subGraphs()) {
            for (const auto &subGraph : gSet) {
                collect(subGraph);
            }
        }
        for (const auto &dep : curr->dependencies()) {
            collect(dep);
        }
        for (Node *node : curr->nodes()) {
            if (node->type() == NodeType::FUNC) {
                auto *funcNode = tt::as_ptr<FuncNode>(node);
                if (funcNode->bodyGraph()) {
                    collect(funcNode->bodyGraph()->shared_from_this());
                }
                continue;
            }
            if (node->type() == NodeType::DATA) {
                auto *dataNode = tt::as_ptr<DataNode>(node);
                if (dataNode->dataType()->code() != TypeCode::Function) {
                    continue;
                }
                auto *func = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
                if (func && func->graph()) {
                    collect(func->graph()->shared_from_this());
                }
            }
        }
    };
    collect(entry->shared_from_this());
    reverse(uniqueGraphs.begin(), uniqueGraphs.end());

    // 编译所有图
    for (auto *graph : uniqueGraphs) {
        size_t start = linked.size();
        std::unordered_map<size_t, camel::source::origin_id_t> localPcOrigins;
        bytecode_vec_t codes = compile(ctx, graph, opt, &localPcOrigins);

        offsetMap[graph] = start;
        graphs.push_back({start, codes.size(), graph});

        linked.insert(linked.end(), codes.begin(), codes.end());
        if (auto sourceContext = ctx ? ctx->sourceContext() : nullptr) {
            for (const auto &[localPc, origin] : localPcOrigins) {
                sourceContext->debugMap().registerPcOrigin(start + localPc, origin);
            }
        }
    }

    // 统一链接 — 修改字节码中的地址引用
    size_t scanIndex    = 0;
    size_t currGraphIdx = 0;
    size_t currGraphEnd = graphs.empty() ? 0 : graphs[0].length;

    while (scanIndex < linked.size()) {
        if (scanIndex >= currGraphEnd && currGraphIdx + 1 < graphs.size()) {
            currGraphIdx++;
            currGraphEnd += graphs[currGraphIdx].length;
        }

        Bytecode &bc              = linked[scanIndex];
        const BytecodeIndex &info = graphs[currGraphIdx];

        switch (bc.opcode) {
        case OpCode::TAIL:
        case OpCode::FUNC: {
            auto *targetGraph = bc.extra()->graph;
            // 写入目标图字节码的起始偏移
            bc.fastop[1] = as_index(offsetMap.at(targetGraph));
        } break;
        case OpCode::JUMP: {
            // 局部偏移转全局偏移
            bc.fastop[0] += offsetMap.at(info.graph);
        } break;
        default:
            break;
        }

        scanIndex += bc.opsize;
    }

    return {linked, graphs, offsetMap};
}

std::string opCodeToString(const Bytecode &bc, const context_ptr_t &context) {
    if (hasDynamicOperands(bc.opcode)) {
        std::string operandStr;

        if (bc.opcode == OpCode::FUNC || bc.opcode == OpCode::TAIL) {
            size_t argsCnt = bc.fastop[0];
            operandStr     = "(";

            for (size_t j = 0; j < argsCnt; j++) {
                operandStr += std::to_string(bc.operands()[j]);
                if (j + 1 < argsCnt)
                    operandStr += ", ";
            }

            operandStr += ")";

            if (bc.fastop[1] >= 0) {
                operandStr += " -> ";
                operandStr += std::to_string(bc.fastop[1]);
            }
        } else {
            size_t normCnt = bc.fastop[0];
            size_t withCnt = bc.fastop[1];
            operandStr     = "(";

            for (size_t j = 0; j < normCnt; j++) {
                operandStr += std::to_string(bc.operands()[j]);
                if (j + 1 < normCnt)
                    operandStr += ", ";
            }

            operandStr += ") <";

            for (size_t j = 0; j < withCnt; j++) {
                operandStr += std::to_string(bc.operands()[normCnt + j]);
                if (j + 1 < withCnt)
                    operandStr += ", ";
            }

            operandStr += ">";
        }

        return std::format(
            "{} | {} | {}",
            bc.toString(),
            operandStr,
            bc.opcode == OpCode::OPER ? context->execMgr().getNameOfAnOperator(bc.extra()->func)
                                      : bc.extra()->toString(bc.opcode));
    } else {
        return std::format("{} | {}", bc.toString(), bc.extra()->toString(bc.opcode));
    }
}
