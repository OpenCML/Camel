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

#include "compile.h"
#include "camel/common/algo/topo.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/func.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <cstddef>

using namespace std;
using namespace GIR;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;

static Node *resolveTailValueNode(Node *outputNode) {
    if (!outputNode) {
        return nullptr;
    }
    if (outputNode->type() != NodeType::GATE) {
        return outputNode;
    }
    // Comment normalized during runtime-graph refactor.
    // Comment normalized during runtime-graph refactor.
    if (!outputNode->normInputs().empty()) {
        return outputNode->normInputs().back();
    }
    return nullptr;
}

static Node *resolveTailValueNode(Graph *graph) {
    return resolveTailValueNode(graph ? graph->outputNode() : nullptr);
}

static GIR::data_idx_t
runtimeDataIndexOf(const camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "FastVM runtime node lookup resolved to null.");
    return node->dataIndex;
}

static camel::runtime::gc_node_ref_t resolveTailValueNode(camel::runtime::GCGraph *graph) {
    if (!graph) {
        return camel::runtime::kInvalidNodeRef;
    }
    auto current = graph->returnNodeRef();
    if (current == camel::runtime::kInvalidNodeRef) {
        current = graph->exitNodeRef();
    }
    while (current != camel::runtime::kInvalidNodeRef) {
        const auto *node = graph->node(current);
        ASSERT(node != nullptr, "FastVM tail-value lookup resolved to null.");
        if (node->kind != camel::runtime::GCNodeKind::Gate) {
            break;
        }
        const auto normInputs = graph->normInputsOf(current);
        if (!normInputs.empty()) {
            current = normInputs.back();
            continue;
        }
        const auto ctrlInputs = graph->ctrlInputsOf(current);
        if (!ctrlInputs.empty()) {
            current = ctrlInputs.back();
            continue;
        }
        return camel::runtime::kInvalidNodeRef;
    }
    return current;
}

static bool runtimeOutputsContain(
    const camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
    camel::runtime::gc_node_ref_t targetRef) {
    auto contains = [targetRef](std::span<const camel::runtime::gc_node_ref_t> refs) {
        return std::find(refs.begin(), refs.end(), targetRef) != refs.end();
    };
    return contains(graph->normOutputsOf(nodeRef)) || contains(graph->withOutputsOf(nodeRef)) ||
           contains(graph->ctrlOutputsOf(nodeRef));
}

static bool emitsRuntimeBytecode(camel::runtime::GCNodeKind kind) {
    switch (kind) {
    case camel::runtime::GCNodeKind::Data:
    case camel::runtime::GCNodeKind::Port:
    case camel::runtime::GCNodeKind::Sync:
    case camel::runtime::GCNodeKind::Gate:
    case camel::runtime::GCNodeKind::Dref:
        return false;
    default:
        return true;
    }
}

static camel::runtime::gc_node_ref_t resolveRuntimeBranchArmEntry(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchRef, size_t armIndex,
    std::span<const camel::runtime::gc_node_ref_t> topoOrder) {
    const auto branchArms = graph->branchArmsOf(brchRef);
    ASSERT(armIndex < branchArms.size(), "Runtime BRCH arm index is out of range.");
    const auto *brchBody = graph->nodeBodyAs<camel::runtime::GCBrchBody>(brchRef);
    ASSERT(brchBody != nullptr, "Runtime BRCH body is missing.");

    std::unordered_set<camel::runtime::gc_node_ref_t> armRegion;
    std::vector<camel::runtime::gc_node_ref_t> worklist{branchArms[armIndex].head};
    while (!worklist.empty()) {
        const auto nodeRef = worklist.back();
        worklist.pop_back();
        if (nodeRef == camel::runtime::kInvalidNodeRef || nodeRef == brchRef ||
            nodeRef == brchBody->join || !graph->containsNodeRef(nodeRef) ||
            !armRegion.insert(nodeRef).second) {
            continue;
        }

        auto pushOutputs = [&](std::span<const camel::runtime::gc_node_ref_t> outputs) {
            for (auto outputRef : outputs) {
                if (outputRef != brchBody->join) {
                    worklist.push_back(outputRef);
                }
            }
        };
        pushOutputs(graph->ctrlOutputsOf(nodeRef));
        pushOutputs(graph->normOutputsOf(nodeRef));
        pushOutputs(graph->withOutputsOf(nodeRef));
    }

    std::unordered_set<camel::runtime::gc_node_ref_t> dependencyVisited;
    std::function<void(camel::runtime::gc_node_ref_t)> collectInputs =
        [&](camel::runtime::gc_node_ref_t nodeRef) {
            if (nodeRef == camel::runtime::kInvalidNodeRef || nodeRef == brchRef ||
                nodeRef == brchBody->join || !graph->containsNodeRef(nodeRef) ||
                !dependencyVisited.insert(nodeRef).second) {
                return;
            }
            armRegion.insert(nodeRef);

            for (auto inputRef : graph->ctrlInputsOf(nodeRef)) {
                collectInputs(inputRef);
            }
            for (auto inputRef : graph->normInputsOf(nodeRef)) {
                collectInputs(inputRef);
            }
            for (auto inputRef : graph->withInputsOf(nodeRef)) {
                collectInputs(inputRef);
            }
        };

    std::vector<camel::runtime::gc_node_ref_t> regionNodes(armRegion.begin(), armRegion.end());
    for (auto nodeRef : regionNodes) {
        collectInputs(nodeRef);
    }
    for (auto nodeRef : topoOrder) {
        if (!armRegion.contains(nodeRef)) {
            continue;
        }
        const auto *node = graph->node(nodeRef);
        if (node && emitsRuntimeBytecode(node->kind)) {
            return nodeRef;
        }
    }
    return brchBody->join;
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
    // Comment normalized during runtime-graph refactor.
    Node *exitNode = graph->exitNode();

    auto topoSortedNodes = findReachable(
        exitNode,
        [](Node *node) {
            vector<Node *> inputs;
            inputs.reserve(node->dataInputs().size() + node->ctrlInputs().size());

            // Comment normalized during runtime-graph refactor.
            for (const auto &in : node->ctrlInputs()) {
                if (&in->graph() == &node->graph())
                    inputs.push_back(in);
            }

            // Comment normalized during runtime-graph refactor.
            for (const auto &in : node->dataInputs()) {
                if (&in->graph() == &node->graph())
                    inputs.push_back(in);
            }

            return inputs;
        },
        false // Do not include the exit node itself in the dependency walk.
    );

    // Comment normalized during runtime-graph refactor.
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
    bytecodes.reserve(topoSortedNodes.size() * 3); // Conservative initial capacity.
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

    // Comment normalized during runtime-graph refactor.
    //
    // Comment normalized during runtime-graph refactor.
    // Comment normalized during runtime-graph refactor.
    // Comment normalized during runtime-graph refactor.
    // Comment normalized during runtime-graph refactor.
    //
    // Comment normalized during runtime-graph refactor.
    unordered_map<Node *, vector<size_t>> brchTargetMap;
    // JOIN*, FROM*
    unordered_map<Node *, vector<pair<size_t, size_t>>> joinTargetMap;
    // Follow value-forwarding wrappers (especially sync GATE nodes) so JOIN
    // backpatching can still upgrade the originating FUNC into TAIL.
    unordered_map<Node *, size_t> valueProducerMap;

    for (size_t i = 0; i < topoSortedNodes.size(); ++i) {
        auto &node = topoSortedNodes[i];

        size_t currIdx             = bytecodes.size();
        const size_t bytecodeStart = bytecodes.size();
        auto sourceContext         = ctx ? ctx->sourceContext() : nullptr;
        auto nodeOrigin            = sourceContext ? sourceContext->resolveGirNodeOrigin(node)
                                                   : camel::source::kInvalidOriginId;

        // Comment normalized during runtime-graph refactor.
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

        // Comment normalized during runtime-graph refactor.
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

            // Comment normalized during runtime-graph refactor.
            // - if/else: withCnt == 0, armCount == 2
            // Comment normalized during runtime-graph refactor.
            const size_t expectedArmCount = withOps.empty() ? 2 : withOps.size() + 1;
            Node *joinFallbackTarget      = nullptr;
            if (brch->hasMatchedJoin() && topoNodeSet.contains(brch->matchedJoin())) {
                joinFallbackTarget = brch->matchedJoin();
            }

            vector<Node *> armHeads;
            armHeads.reserve(expectedArmCount);

            // Comment normalized during runtime-graph refactor.
            // Comment normalized during runtime-graph refactor.
            // Comment normalized during runtime-graph refactor.
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
                // Comment normalized during runtime-graph refactor.
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

            // Comment normalized during runtime-graph refactor.
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
            // Comment normalized during runtime-graph refactor.
            // Comment normalized during runtime-graph refactor.
            // Comment normalized during runtime-graph refactor.
            // Comment normalized during runtime-graph refactor.
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
            // Comment normalized during runtime-graph refactor.
            // Comment normalized during runtime-graph refactor.
            normOps.insert(normOps.end(), withOps.begin(), withOps.end());
            appendBytecode(
                bytecodes,
                (opt.enableTailCallDetection && isTail) ? OpCode::TAIL : OpCode::FUNC,
                node->index(),
                {},
                normOps,
                {},
                true,
                {.sourceGraph = funcNode->bodyGraph()},
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

            // Comment normalized during runtime-graph refactor.
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
            if (!node->normInputs().empty()) {
                Node *valueInput = node->normInputs().back();
                if (auto it = valueProducerMap.find(valueInput); it != valueProducerMap.end()) {
                    valueProducerMap[node] = it->second;
                }
            }
            // Comment normalized during runtime-graph refactor.
            // Comment normalized during runtime-graph refactor.
            // Comment normalized during runtime-graph refactor.
            // Comment normalized during runtime-graph refactor.
            break; // This must remain break, not continue.
        }

        case NodeType::PORT:
            [[fallthrough]];
        case NodeType::SYNC:
            [[fallthrough]];
        case NodeType::DREF:
            // Comment normalized during runtime-graph refactor.
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

        if (bytecodes.size() > bytecodeStart) {
            valueProducerMap[node] = currIdx;
        }

        // Comment normalized during runtime-graph refactor.
        if (node->hasMatchedJoinOutput()) {
            auto *joinNode = node->matchedJoinOutput();
            size_t fromIdx = currIdx;
            if (auto it = valueProducerMap.find(node); it != valueProducerMap.end()) {
                fromIdx = it->second;
            }
            joinTargetMap[joinNode].push_back({
                bytecodes.size(),
                fromIdx,
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

    // Comment normalized during runtime-graph refactor.
    BytecodeOptimizer optimizer(opt.optimizationStrategies);
    optimizer.optimize(bytecodes, 0, localPcOrigins);

    return bytecodes;
}

static bytecode_vec_t compileRuntimeGraph(
    const context_ptr_t &ctx, camel::runtime::GCGraph *graph, const CompileStrategy &opt,
    std::unordered_map<size_t, camel::source::origin_id_t> *localPcOrigins) {
    ASSERT(graph != nullptr, "FastVM compile requires a non-null runtime graph.");
    ASSERT(
        graph->hasNodePayload(),
        std::format("Runtime graph '{}' has no node payload.", graph->name()));

    auto topoSortedIndices   = camel::execute::buildReachableExecutionTopoIndices(graph);
    const auto returnNodeRef = graph->returnNodeRef();
    const auto *returnRecord = graph->returnNode();
    ASSERT(returnRecord != nullptr, "Runtime graph return node is null.");
    ASSERT(returnRecord->dataIndex != 0, "Runtime graph return slot is invalid.");

    EXEC_WHEN_DEBUG({
        CAMEL_LOG_DEBUG_S("Topo", "Topologically sorted nodes for graph {}:", graph->name());
        for (auto nodeRef : topoSortedIndices) {
            const auto *node = graph->node(nodeRef);
            CAMEL_LOG_DEBUG_S(
                "Topo",
                "  ref={} kind={} slot={}",
                nodeRef,
                static_cast<int>(node ? node->kind : camel::runtime::GCNodeKind::Data),
                node ? node->dataIndex : 0);
        }
    });

    auto bytecodes = bytecode_vec_t();
    bytecodes.reserve(topoSortedIndices.size() * 3);
    std::unordered_set<camel::runtime::gc_node_ref_t> topoNodeSet(
        topoSortedIndices.begin(),
        topoSortedIndices.end());
    const auto tailValueNode       = resolveTailValueNode(graph);
    auto hasOnlyTrivialSuffixAfter = [&](size_t index) {
        for (size_t j = index + 1; j < topoSortedIndices.size(); ++j) {
            const auto *suffixNode = graph->node(topoSortedIndices[j]);
            if (!suffixNode || suffixNode->kind != camel::runtime::GCNodeKind::Gate) {
                return false;
            }
        }
        return true;
    };

    unordered_map<camel::runtime::gc_node_ref_t, vector<size_t>> brchTargetMap;
    unordered_map<camel::runtime::gc_node_ref_t, vector<pair<size_t, size_t>>> joinTargetMap;
    unordered_map<camel::runtime::gc_node_ref_t, size_t> valueProducerMap;

    for (size_t i = 0; i < topoSortedIndices.size(); ++i) {
        const auto runtimeNodeIndex = topoSortedIndices[i];
        const auto *record          = graph->node(runtimeNodeIndex);
        ASSERT(record != nullptr, "Runtime topo node is missing runtime metadata.");

        size_t currIdx             = bytecodes.size();
        const size_t bytecodeStart = bytecodes.size();
        const auto nodeOrigin      = camel::source::kInvalidOriginId;

        if (auto it = brchTargetMap.find(runtimeNodeIndex); it != brchTargetMap.end()) {
            for (size_t jumpIndex : it->second) {
                auto &header     = bytecodes[jumpIndex];
                header.fastop[0] = as_index(bytecodes.size());
            }
            brchTargetMap.erase(it);
        }

        vector<data_idx_t> normOps, withOps;
        for (auto inputRef : graph->normInputsOf(runtimeNodeIndex)) {
            normOps.push_back(runtimeDataIndexOf(graph, inputRef));
        }
        for (auto inputRef : graph->withInputsOf(runtimeNodeIndex)) {
            withOps.push_back(runtimeDataIndexOf(graph, inputRef));
        }

        switch (record->kind) {
        case camel::runtime::GCNodeKind::Cast: {
            ASSERT(!normOps.empty(), "CAST node must have one norm input.");
            Type *targetType = record->dataType;
            BytecodeExtra extra;
            extra.pType = targetType;
            appendBytecode(
                bytecodes,
                OpCode::CAST,
                record->dataIndex,
                {normOps.front()},
                {},
                {},
                true,
                extra);
            break;
        }

        case camel::runtime::GCNodeKind::Copy:
            appendBytecode(bytecodes, OpCode::COPY, record->dataIndex, {normOps.front()});
            break;

        case camel::runtime::GCNodeKind::Fill:
            appendBytecode(bytecodes, OpCode::FILL, record->dataIndex, {}, normOps, withOps);
            break;

        case camel::runtime::GCNodeKind::Accs: {
            ASSERT(!normOps.empty(), "ACCS node must have one norm input.");
            const auto sourceRef  = graph->normInputsOf(runtimeNodeIndex).front();
            const auto *srcRecord = graph->node(sourceRef);
            const auto *accBody   = graph->nodeBodyAs<camel::runtime::GCAccsBody>(runtimeNodeIndex);
            ASSERT(
                srcRecord != nullptr && srcRecord->dataType != nullptr,
                "ACCS source node must exist.");
            ASSERT(srcRecord->dataType->isComposite(), "ACCS source node must be composite.");

            size_t index = 0;

            switch (srcRecord->dataType->code()) {
            case TypeCode::Tuple: {
                ASSERT(
                    accBody->accsKind == camel::runtime::GCAccsKind::TupleIndex,
                    "ACCS tuple access must be numeric.");
                index                 = accBody->value;
                const auto &tupleType = tt::as_ptr<TupleType>(srcRecord->dataType);
                if (index >= tupleType->size()) {
                    ctx->rtmDiags()->of(SemanticDiag::InvalidAccessIndex).commit(to_string(index));
                    index = 0;
                }
                break;
            }
            case TypeCode::Struct: {
                ASSERT(
                    accBody->accsKind == camel::runtime::GCAccsKind::StructKey,
                    "ACCS struct access must be keyed.");
                const std::string key(accBody->key());
                const auto *structType =
                    tt::as_ptr<camel::core::type::StructType>(srcRecord->dataType);
                const auto &optIndex = structType->findField(key);
                if (!optIndex.has_value()) {
                    ctx->rtmDiags()->of(SemanticDiag::InvalidAccessIndex).commit(key);
                    index = 0;
                } else {
                    index = optIndex.value();
                }
                break;
            }
            default:
                ASSERT(false, "Unsupported ACCS source node type.");
            }

            appendBytecode(
                bytecodes,
                OpCode::ACCS,
                record->dataIndex,
                {
                    normOps.front(),
                    as_index(index),
                });
            break;
        }

        case camel::runtime::GCNodeKind::Brch: {
            appendBytecode(bytecodes, OpCode::BRCH, record->dataIndex, {}, normOps, withOps);
            const auto branchArms = graph->branchArmsOf(runtimeNodeIndex);
            for (size_t armIndex = 0; armIndex < branchArms.size(); ++armIndex) {
                const auto armEntry = resolveRuntimeBranchArmEntry(
                    graph,
                    runtimeNodeIndex,
                    armIndex,
                    topoSortedIndices);
                ASSERT(
                    armEntry != camel::runtime::kInvalidNodeRef,
                    std::format(
                        "FastVM runtime compile cannot resolve BRCH arm {} entry in graph '{}'.",
                        armIndex,
                        graph->name()));
                brchTargetMap[armEntry].push_back(bytecodes.size());
                appendBytecode(bytecodes, OpCode::JUMP, 0, {0});
            }
            if (localPcOrigins && nodeOrigin != camel::source::kInvalidOriginId) {
                (*localPcOrigins)[currIdx] = nodeOrigin;
            }

            continue;
        }

        case camel::runtime::GCNodeKind::Join: {
            bool isTail = runtimeNodeIndex == tailValueNode && hasOnlyTrivialSuffixAfter(i);

            if (joinTargetMap.find(runtimeNodeIndex) != joinTargetMap.end()) {
                for (const auto &[jumpIdx, fromIdx] : joinTargetMap[runtimeNodeIndex]) {
                    auto &jump     = bytecodes[jumpIdx];
                    auto &from     = bytecodes[fromIdx];
                    jump.fastop[0] = as_index(bytecodes.size());
                    if (opt.enableTailCallDetection && isTail && from.opcode == OpCode::FUNC) {
                        from.opcode = OpCode::TAIL;
                    }
                }
                joinTargetMap.erase(runtimeNodeIndex);
            }

            appendBytecode(bytecodes, OpCode::JOIN, record->dataIndex, {}, normOps, withOps);

            break;
        }

        case camel::runtime::GCNodeKind::Call:
            appendBytecode(bytecodes, OpCode::CALL, record->dataIndex, {}, normOps, withOps);
            break;

        case camel::runtime::GCNodeKind::Bind:
            ASSERT(false, "BIND node not implemented.");
            break;

        case camel::runtime::GCNodeKind::Func: {
            bool isTail = runtimeNodeIndex == tailValueNode && hasOnlyTrivialSuffixAfter(i);
            auto *targetRuntimeGraph = graph->directCalleeGraphOf(runtimeNodeIndex);
            ASSERT(
                targetRuntimeGraph != nullptr,
                std::format(
                    "FastVM runtime compile cannot resolve direct runtime callee for node ref {} "
                    "in graph '{}'.",
                    runtimeNodeIndex,
                    graph->name()));
            normOps.insert(normOps.end(), withOps.begin(), withOps.end());
            appendBytecode(
                bytecodes,
                (opt.enableTailCallDetection && isTail) ? OpCode::TAIL : OpCode::FUNC,
                record->dataIndex,
                {},
                normOps,
                {},
                true,
                {.runtimeGraph = targetRuntimeGraph},
#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
                2
#else
                1
#endif
            );
            break;
        }

        case camel::runtime::GCNodeKind::Oper: {
            const auto *operBody = graph->nodeBodyAs<camel::runtime::GCOperBody>(runtimeNodeIndex);
            const std::string uri(operBody->uri());

            if (opt.enableInlineOperators) {
                const auto &inlineOpMap = getSupportedInlineOperatorsMap();
                auto it                 = inlineOpMap.find(uri);
                if (it != inlineOpMap.end()) {
                    appendBytecode(
                        bytecodes,
                        it->second,
                        record->dataIndex,
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
                    record->dataIndex,
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
                record->dataIndex,
                {},
                normOps,
                withOps,
                true,
                {
                    .func = *opFunc,
                });
            break;
        }

        case camel::runtime::GCNodeKind::Data:
            [[fallthrough]];
        case camel::runtime::GCNodeKind::Gate: {
            const auto normInputs = graph->normInputsOf(runtimeNodeIndex);
            if (!normInputs.empty()) {
                const auto valueInput = normInputs.back();
                if (auto it = valueProducerMap.find(valueInput); it != valueProducerMap.end()) {
                    valueProducerMap[runtimeNodeIndex] = it->second;
                }
            }
            break;
        }

        case camel::runtime::GCNodeKind::Port:
            [[fallthrough]];
        case camel::runtime::GCNodeKind::Sync:
            [[fallthrough]];
        case camel::runtime::GCNodeKind::Dref:
            continue;

        default:
            ASSERT(
                false,
                std::format(
                    "Unsupported node type encountered in runtime bytecode generation: {}",
                    static_cast<int>(record->kind)));
        }

        if (localPcOrigins && nodeOrigin != camel::source::kInvalidOriginId) {
            (*localPcOrigins)[currIdx] = nodeOrigin;
        }

        if (bytecodes.size() > bytecodeStart) {
            valueProducerMap[runtimeNodeIndex] = currIdx;
        }

        if (record->kind == camel::runtime::GCNodeKind::Func) {
            const auto joinNode = graph->matchedJoinOutputOf(runtimeNodeIndex);
            if (joinNode == camel::runtime::kInvalidNodeRef) {
                continue;
            }
            size_t fromIdx = currIdx;
            if (auto it = valueProducerMap.find(runtimeNodeIndex); it != valueProducerMap.end()) {
                fromIdx = it->second;
            }
            joinTargetMap[joinNode].push_back({
                bytecodes.size(),
                fromIdx,
            });
            appendBytecode(bytecodes, OpCode::JUMP, 0, {0});
        }
    }

    appendBytecode(
        bytecodes,
        OpCode::RETN,
        0,
        {static_cast<data_idx_t>(runtimeDataIndexOf(graph, returnNodeRef))});

    ASSERT(
        brchTargetMap.empty(),
        "Some BRCH nodes have unmatched control outputs without corresponding JOIN nodes.");
    ASSERT(
        joinTargetMap.empty(),
        "Some JOIN nodes have unmatched JUMP instructions without corresponding targets.");

    BytecodeOptimizer optimizer(opt.optimizationStrategies);
    optimizer.optimize(bytecodes, 0, localPcOrigins);

    return bytecodes;
}

bytecode_vec_t compile(
    const context_ptr_t &ctx, camel::runtime::GCGraph *graph, const CompileStrategy &opt,
    std::unordered_map<size_t, camel::source::origin_id_t> *localPcOrigins) {
    return compileRuntimeGraph(ctx, graph, opt, localPcOrigins);
}

LinkedBytecodeResult
compileAndLink(context_ptr_t ctx, camel::runtime::GCGraph *entry, const CompileStrategy &opt) {
    bytecode_vec_t linked;
    std::vector<BytecodeIndex> graphs;
    std::unordered_map<camel::runtime::GCGraph *, size_t> offsetMap;

    std::vector<camel::runtime::GCGraph *> uniqueGraphs =
        camel::runtime::collectReachableGraphs(entry);
    reverse(uniqueGraphs.begin(), uniqueGraphs.end());

    for (auto *runtimeGraph : uniqueGraphs) {
        ASSERT(runtimeGraph != nullptr, "Reachable runtime graph set contains null.");
        size_t start = linked.size();
        std::unordered_map<size_t, camel::source::origin_id_t> localPcOrigins;
        bytecode_vec_t codes = compile(ctx, runtimeGraph, opt, &localPcOrigins);

        offsetMap[runtimeGraph] = start;
        graphs.push_back({start, codes.size(), runtimeGraph});

        linked.insert(linked.end(), codes.begin(), codes.end());
        if (auto sourceContext = ctx ? ctx->sourceContext() : nullptr) {
            for (const auto &[localPc, origin] : localPcOrigins) {
                sourceContext->debugMap().registerPcOrigin(start + localPc, origin);
            }
        }
    }

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
            ASSERT(
                getFuncExtraRuntimeGraph(&bc) != nullptr,
                std::format(
                    "FastVM linker cannot resolve runtime graph for bytecode at pc {}.",
                    scanIndex));
            bc.fastop[1] = as_index(offsetMap.at(getFuncExtraRuntimeGraph(&bc)));
        } break;
        case OpCode::JUMP: {
            bc.fastop[0] += offsetMap.at(info.runtimeGraph);
        } break;
        default:
            break;
        }

        scanIndex += bc.opsize;
    }

    return {
        .codes     = std::move(linked),
        .graphs    = std::move(graphs),
        .offsetMap = std::move(offsetMap),
    };
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
