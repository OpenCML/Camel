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
 * Created: Sep. 08, 2025
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "nodevm.h"
#include "camel/common/algo/topo.h"
#include "camel/compile/gir/graph.h"
#include "camel/compile/gir/nodes.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/global_config.h"
#include "camel/core/module/module.h"
#include "camel/core/operator.h"
#include "camel/execute/executor.h"
#include "camel/utils/log.h"

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"

using namespace std;
using namespace GIR;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using namespace camel::core::error;

NodeVMSchedPass::~NodeVMSchedPass() {
    for (Graph *g : graphsWithTopoCache_) {
        g->setExtra<node_vec_t, kTopoNodesExtraIndex>(nullptr);
    }
}

std::span<Node *> NodeVMSchedPass::buildTopoNodes(Graph *graph) {
    ASSERT(
        graph->finalized(),
        std::format("Graph {} is not finalized before executing.", graph->name()));

    Node *exitNode   = graph->exitNode();
    auto sortedNodes = findReachable(
        exitNode,
        [](Node *n) {
            node_vec_t ins;
            ins.reserve(n->dataInputs().size() + n->ctrlInputs().size());
            for (const auto &in : n->ctrlInputs()) {
                if (&in->graph() == &n->graph()) // only consider nodes in the same graph
                    ins.emplace_back(in);
            }
            // Put value computation nodes at the back for correct tail-call optimization
            for (const auto &in : n->dataInputs()) {
                if (&in->graph() == &n->graph()) // only consider nodes in the same graph
                    ins.emplace_back(in);
            }
            return ins;
        },
        true // skip the start node itself
    );

    EXEC_WHEN_DEBUG({
        GetDefaultLogger().in("Topo").debug(
            "Topologically sorted nodes for graph {}:",
            graph->name());
        for (const auto &n : sortedNodes) {
            GetDefaultLogger().in("Topo").debug("  {}", n->toString());
        }
        size_t totalNodeCnt =
            graph->nodes().size() + graph->ports().size() + graph->closure().size();
        if (sortedNodes.size() != totalNodeCnt) {
            GIR::node_vec_t unreachableNodes;
            for (Node *n : graph->nodes()) {
                if (n != exitNode &&
                    std::find(sortedNodes.begin(), sortedNodes.end(), n) == sortedNodes.end()) {
                    unreachableNodes.push_back(n);
                }
            }
            std::string nodeStrs;
            for (const auto &node : unreachableNodes) {
                if (!nodeStrs.empty()) {
                    nodeStrs += ", ";
                }
                nodeStrs += node->toString();
            }
            GetDefaultLogger().in("Topo").warn(
                "Unreachable nodes in graph {} detected: {}",
                graph->name(),
                nodeStrs);
        }
    });

    auto &vec = topoNodesOwned_[graph];
    vec       = std::move(sortedNodes);
    graph->setExtra<node_vec_t, kTopoNodesExtraIndex>(&vec);
    graphsWithTopoCache_.insert(graph);
    return std::span(vec);
}

// =============================================================================
// 尾调用优化：互调用涉及第三方函数时的帧管理三种情形
// =============================================================================
//
// 设 A 为根帧（root），A、B 互相尾调用，并在中途调用 C。
//
// 情形一：A 或 B 普通调用 C，C 返回后正常释放 C 的帧即可
//   ┌────────┬────────┬────────┐
//   │  (A)   │  (B)   │  (C)   │
//   └────────┴────────┴────────┘
//       ↑                  ↑
//     root1              root2  （C 的帧在 Call 内 acquire/release）
//
// 情形二：A 在中途尾调用 C，此时孪生帧 twin 指向 B；需先释放孪生帧 B，再申请 C 的帧
//   ┌────────┬────────┐
//   │  (A)   │  (C)   │
//   └────────┴────────┘
//       ↑         ↑
//     root      curr
//       ↑
//   twin(B→A)
//
// 情形三：B 在中途尾调用 C，此时孪生帧 twin 指向 A，不能释放根帧 A，只能在退出 C++ 栈帧时依次释放
//   ┌────────┬────────┬────────┐
//   │  (A)   │  (B)   │  (C)   │
//   └────────┴────────┴────────┘
//       ↑         ↑         ↑
//     root   twin(A→B)   curr
//
// 释放顺序（退出 call 时）：
//   1. 若 curr != root，先释放 curr
//   2. 若 twin != nullptr 且 twin != root，释放 twin
//   3. 最后释放 root（call 拥有 rootFrame 的所有权，由 call 负责释放）
// =============================================================================

// 将节点 n（CALL 或 FUNC）的参数从 source 帧复制到 dest 帧（targetGraph 的端口与闭包）
static inline void fillFrameForFunc(Frame *from, Frame *dest, Graph *graph, Node *node) {
    using namespace GIR;

    const auto &normNodes = node->normInputs();
    const auto &normPorts = graph->normPorts();
    ASSERT(
        normNodes.size() == normPorts.size(),
        "Norm nodes and ports count mismatch in fillFrameForFunc.");
    for (size_t i = 0; i < normNodes.size(); ++i) {
        dest->set(normPorts[i]->index(), from->get<slot_t>(normNodes[i]->index()));
    }

    const auto &withNodes = node->withInputs();
    const auto &withPorts = graph->withPorts();
    ASSERT(
        withNodes.size() == withPorts.size(),
        "With nodes and ports count mismatch in fillFrameForFunc.");
    for (size_t i = 0; i < withNodes.size(); ++i) {
        dest->set(withPorts[i]->index(), from->get<slot_t>(withNodes[i]->index()));
    }
}

slot_t NodeVMSchedPass::call(Graph *rootGraph, Frame *rootFrame) {
    currRecursionDepth_++;
    slot_t result = NullSlot;

    // currFrame：当前执行的栈帧；twinFrame：孪生帧，用于互尾递归（A 尾调 B、B 尾调 A）时复用
    Frame *currFrame  = rootFrame;
    Frame *twinFrame  = nullptr;
    Graph *currGraph  = rootGraph;
    Node *currentNode = nullptr;

    try {
        if (currRecursionDepth_ > maxRecursionDepth_) {
            throwRuntimeFault(
                RuntimeDiag::MaxRecursionDepthExceeded,
                rootGraph->name(),
                maxRecursionDepth_);
        }

        auto *nodesVec              = currGraph->getExtra<node_vec_t, kTopoNodesExtraIndex>();
        std::span<Node *> currNodes = nodesVec ? std::span(*nodesVec) : buildTopoNodes(currGraph);

        // 用于实现分支跳转
        // tillNode 如果不为空，则跳过 tillNode 前所有节点
        // skipNode 如果不为空，则执行到 skipNode 为止，而后将 joinNode 设置为新的 tillNode
        // 这样 VM 就会跳过剩余节点，直到目标的 Join 节点
        Node *tillNode = nullptr, *skipNode = nullptr, *joinNode = nullptr;

    // 尾调用优化主循环：不退出 C++ 栈帧，用新的 currGraph/currFrame 继续执行
    loop_start: {
        const size_t nodesSize = currNodes.size();
        Node *lastNode         = currGraph->outputNode();
        if (!lastNode->ctrlOutputs().empty()) {
            // 如果最后一个值节点有控制输出
            // 说明在最后一个值节点和退出节点之间存在有副作用但不产生返回值的分支
            // 那这也意味着推出节点必然会有控制边输入
            // 需要从退出节点回溯找到最后一个控制边输入的节点
            ASSERT(
                !currGraph->exitNode()->ctrlInputs().empty(),
                "Exit node has no control inputs.");
            lastNode = currGraph->exitNode()->ctrlInputs().back();
        }
        bool lastNodeIsJoin = lastNode->type() == NodeType::JOIN;

        size_t i = 0;
        for (; i < nodesSize; ++i) {
            Node *n     = currNodes[i];
            currentNode = n;
            if (InternalGlobalConfig::IsInspectionMode() && context_) {
                if (auto sourceContext = context_->sourceContext()) {
                    sourceContext->setCurrentRuntimeOrigin(sourceContext->resolveGirNodeOrigin(n));
                }
            }

            if (tillNode) {
                if (tillNode == n) {
                    EXEC_WHEN_DEBUG(
                        GetDefaultLogger().in("NodeVM").debug(
                            "Reached tillNode [{}/{}] graph={}: {}",
                            i + 1,
                            currNodes.size(),
                            currGraph->name(),
                            n->toString()));
                    tillNode = nullptr;
                } else {
                    EXEC_WHEN_DEBUG(
                        GetDefaultLogger().in("NodeVM").debug(
                            "Skipping node [{}/{}] graph={}: {}",
                            i + 1,
                            currNodes.size(),
                            currGraph->name(),
                            n->toString()));
                    continue;
                }
            }
            if (skipNode && skipNode == n) {
                EXEC_WHEN_DEBUG(
                    GetDefaultLogger().in("NodeVM").debug(
                        "Reached skipNode [{}/{}] graph={}: {}",
                        i + 1,
                        currNodes.size(),
                        currGraph->name(),
                        n->toString()));
                skipNode = nullptr;
                tillNode = joinNode;
            }

            EXEC_WHEN_DEBUG({
                GetDefaultLogger().in("NodeVM").debug(
                    "Executing node [{}/{}] graph={}: {}",
                    i + 1,
                    currNodes.size(),
                    currGraph->name(),
                    n->toString());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });

            switch (n->type()) {
            case NodeType::CAST: {
                const auto &inputNode = n->normInputs().front();
                Type *srcType         = currFrame->typeAt<Type>(inputNode->index());
                Type *tgtType         = n->dataType();
                slot_t value          = currFrame->get<slot_t>(inputNode->index());
                slot_t result         = tgtType->castSlotFrom(value, srcType);
                currFrame->set(n->index(), result);
            } break;

            case NodeType::COPY: {
                const auto &inputNode = n->normInputs().front();
                data_idx_t srcIdx     = inputNode->index();
                TypeCode srcCode      = currFrame->codeAt(srcIdx);
                if (isGCTraced(srcCode)) {
                    Object *srcData  = currFrame->get<Object *>(srcIdx);
                    Type *srcTypePtr = currFrame->typeAt<Type>(srcIdx);
                    currFrame->set(n->index(), srcData->clone(mm::autoSpace(), srcTypePtr, false));
                } else {
                    currFrame->set(n->index(), currFrame->get<slot_t>(srcIdx));
                }
            } break;

            case NodeType::FILL: {
                const auto &srcNode    = n->normInputs().front();
                const auto &dataInputs = n->withInputs();
                TypeCode srcCode       = currFrame->codeAt(srcNode->index());
                Type *srcType          = currFrame->typeAt<Type>(srcNode->index());
                ASSERT(isGCTraced(srcCode), "FILL target type is not GC-traced in NodeVM.");
                Object *srcObj = currFrame->get<Object *>(srcNode->index())
                                     ->clone(mm::autoSpace(), srcType, false);
                ASSERT(srcObj != nullptr, "FILL target data is null.");

                switch (srcCode) {
                case TypeCode::Tuple: {
                    auto type          = tt::as_ptr<TupleType>(srcType);
                    auto tup           = tt::as_ptr<Tuple>(srcObj);
                    const size_t *refs = type->refs();
                    for (size_t j = 0; j < dataInputs.size(); ++j) {
                        tup->set<slot_t>(refs[j], currFrame->get<slot_t>(dataInputs[j]->index()));
                    }
                } break;
                case TypeCode::Array: {
                    auto arr = tt::as_ptr<Array>(srcObj);
                    for (size_t j = 0; j < dataInputs.size(); ++j) {
                        arr->set<slot_t>(j, currFrame->get<slot_t>(dataInputs[j]->index()));
                    }
                } break;
                case TypeCode::Struct: {
                    auto type          = tt::as_ptr<StructType>(srcType);
                    auto str           = tt::as_ptr<Struct>(srcObj);
                    const size_t *refs = type->refs();
                    for (size_t j = 0; j < dataInputs.size(); ++j) {
                        str->set<slot_t>(refs[j], currFrame->get<slot_t>(dataInputs[j]->index()));
                    }
                } break;
                case TypeCode::Function: {
                    auto func          = tt::as_ptr<Function>(srcObj);
                    Tuple *closureData = func->tuple();
                    ASSERT(closureData != nullptr, "Closure data is null in FILL.");
                    ASSERT(
                        closureData->size() == dataInputs.size(),
                        "Closure data size mismatch in FILL.");
                    for (size_t j = 0; j < dataInputs.size(); ++j) {
                        closureData->set<slot_t>(j, currFrame->get<slot_t>(dataInputs[j]->index()));
                    }
                } break;
                default:
                    ASSERT(
                        false,
                        std::format(
                            "Unsupported FILL target type {} in NodeVM.",
                            typeCodeToString(srcCode)));
                }
                currFrame->set(n->index(), srcObj);
            } break;

            case NodeType::ACCS: {
                auto accsNode     = tt::as_ptr<AccsNode>(n);
                data_idx_t srcIdx = n->dataInputs().front()->index();
                if (accsNode->isNum()) {
                    size_t idx = accsNode->numIndex();
                    Tuple *t   = currFrame->get<Tuple *>(srcIdx);
                    ASSERT(idx < t->size(), "Tuple index out of bounds in NodeVM.");
                    currFrame->set(n->index(), t->get<slot_t>(idx));
                } else {
                    std::string key  = accsNode->strIndex();
                    Struct *s        = currFrame->get<Struct *>(srcIdx);
                    Type *structType = currFrame->typeAt<Type>(srcIdx);
                    currFrame->set(n->index(), s->get<slot_t>(key, structType));
                }
            } break;

            case NodeType::BRCH: {
                const auto &normIns = n->normInputs();
                const auto &withIns = n->withInputs();
                ASSERT(normIns.size() == 1, "Branch node must have exactly one norm input.");

                size_t jumpIdx = 0;
                if (withIns.empty()) {
                    bool cond = currFrame->get<bool>(normIns.front()->index());
                    jumpIdx   = cond ? 0 : 1;
                } else {
                    TypeCode condType = currFrame->codeAt(normIns.front()->index());
                    size_t j          = 0;
                    if (isGCTraced(condType)) {
                        Type *condTypePtr = currFrame->typeAt<Type>(normIns.front()->index());
                        Object *condData  = currFrame->get<Object *>(normIns.front()->index());
                        for (; j < withIns.size(); ++j) {
                            Object *caseData = currFrame->get<Object *>(withIns[j]->index());
                            if (condData->equals(caseData, condTypePtr, false)) {
                                jumpIdx = j;
                                break;
                            }
                        }
                    } else {
                        slot_t condData = currFrame->get<slot_t>(normIns.front()->index());
                        for (; j < withIns.size(); ++j) {
                            if (condData == currFrame->get<slot_t>(withIns[j]->index())) {
                                jumpIdx = j;
                                break;
                            }
                        }
                    }
                    if (j == withIns.size())
                        jumpIdx = withIns.size();
                }
                currFrame->set(n->index(), fromSlot<Int32>(static_cast<Int32>(jumpIdx)));

                // BRCH 有且仅有 1 个 normOutput，即对应的 JOIN
                auto *brchNode   = tt::as_ptr<BrchNode>(n);
                auto *targetJoin = brchNode->matchedJoin();

                // 分支跳转：跳到选中分支的 head，顺序执行到 tail，再跳到 JOIN
                tillNode = brchNode->armHead(jumpIdx); // 选中分支的头节点
                skipNode =
                    targetJoin->armTail(jumpIdx); // 选中分支的尾节点（连到 JOIN 的 with 输入）
                joinNode = targetJoin;

                EXEC_WHEN_DEBUG(
                    GetDefaultLogger().in("NodeVM").debug(
                        "BRCH node {}: jumpIdx={}, branches={}, tillNode={}, skipNode={}, "
                        "joinNode={}",
                        n->toString(),
                        jumpIdx,
                        brchNode->armCount(),
                        tillNode->toString(),
                        skipNode->toString(),
                        joinNode->toString()));
            } break;

            case NodeType::JOIN: {
                const auto &nargs = n->normInputs();
                const auto &wargs = n->withInputs();
                ASSERT(!nargs.empty(), "JOIN must have norm input (branch index).");
                int32_t brIndex = currFrame->get<int32_t>(nargs.front()->index());
                ASSERT(
                    brIndex >= 0 && static_cast<size_t>(brIndex) < wargs.size(),
                    "JOIN branch index out of range in NodeVM.");
                // 分支已通过 tillNode/skipNode 顺序执行完毕，结果在对应分支尾节点的槽中
                slot_t branchResult =
                    currFrame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]->index());
                currFrame->set(n->index(), branchResult);
            } break;

            case NodeType::CALL: {
                auto *callNode = tt::as_ptr<CallNode>(n);
                ASSERT(callNode->hasCallee(), "CALL node must have exactly one callee input");
                const auto &funcNode = callNode->calleeInput();
                Function *func       = currFrame->get<Function *>(funcNode->index());
                Graph *funcGraph     = func->graph();

                const auto &normNodes = n->normInputs();
                const auto &normPorts = funcGraph->normPorts();
                ASSERT(
                    normNodes.size() == normPorts.size(),
                    "Norm nodes and ports count mismatch in fillFrameForCall.");

                Frame *funcFrame = framePool_.acquire(funcGraph);
                for (size_t i = 0; i < normNodes.size(); ++i) {
                    funcFrame->set(
                        normPorts[i]->index(),
                        currFrame->get<slot_t>(normNodes[i]->index()));
                }

                if (funcGraph->hasClosure()) {
                    // 处理闭包
                    Tuple *closureData = func->tuple();
                    ASSERT(closureData != nullptr, "Closure is null in fillFrameForCall.");
                    const auto &closureNodes = funcGraph->closure();
                    ASSERT(
                        closureNodes.size() == closureData->size(),
                        "Closure nodes and tuple size mismatch in fillFrameForCall.");
                    for (size_t j = 0; j < closureNodes.size(); ++j) {
                        funcFrame->set(closureNodes[j]->index(), closureData->get<slot_t>(j));
                    }
                }

                slot_t callResult = call(funcGraph, funcFrame);
                currFrame->set(n->index(), callResult);
            } break;

            case NodeType::FUNC: {
                Graph *funcGraph = tt::as_ptr<FuncNode>(n)->bodyGraph();

                // 尾调用优化
                bool isTailCall =
                    n == lastNode || (lastNodeIsJoin && tt::as_ptr<FuncNode>(n)->hasMatchedJoin() &&
                                      tt::as_ptr<FuncNode>(n)->matchedJoin() == lastNode);
                if (isTailCall) {
                    EXEC_WHEN_DEBUG(
                        GetDefaultLogger().in("NodeVM").debug(
                            "Optimizing tail-call for node [{}/{}] graph={}: {}",
                            i + 1,
                            currNodes.size(),
                            currGraph->name(),
                            n->toString()));
                    // enable tail-call optimization
                    // 当前拓扑节点序列执行子循环结束后不会退出大循环
                    // 这样可以复用当前 C++ 栈帧，避免 C++ 栈溢出
                    // 设置 lastFrame 为当前帧 currFrame 备用
                    Frame *lastFrame = currFrame;
                    // 清空 tillNode/skipNode/joinNode
                    tillNode = nullptr;
                    skipNode = nullptr;

                    // 下面准备将 currFrame 重新指向新栈帧
                    if (funcGraph == currGraph) {
                        // 如果目标图就是当前帧的图，说明在进行自递归尾调用
                        // 此时可以复用当前栈帧和字节码，无需修改栈帧指向
                        EXEC_WHEN_DEBUG(
                            GetDefaultLogger().in("NodeVM").debug(
                                "Optimizing self-recursion for graph: {}",
                                currFrame->graph()->name()));
                    } else {
                        // 否则需要切换到目标图的节点序列，并修改 currGraph 指向
                        nodesVec  = funcGraph->getExtra<node_vec_t, kTopoNodesExtraIndex>();
                        currNodes = nodesVec ? std::span(*nodesVec) : buildTopoNodes(funcGraph);
                        currGraph = funcGraph;

                        // 即便目标图不是当前图，仍可能存在互调用尾递归现象
                        // 即 A 尾调用 B，B 又尾调用 A
                        // Camel 中分支默认被编译为子图，互调用非常常见，必须针对性优化
                        // 思路是在互调用时维护一个孪生栈帧 twinFrame
                        // 在执行 A 时将孪生栈帧指向 B，同理在执行 B 时将其指向 A
                        // 复用 C++ 的栈帧和循环，但在 A/B 两个栈帧中切换
                        if (twinFrame && twinFrame->graph() == funcGraph) {
                            // 如果缓存的孪生栈帧刚好是目标栈帧，则复用
                            EXEC_WHEN_DEBUG(
                                GetDefaultLogger().in("NodeVM").debug(
                                    "Optimizing mutual-tail-recursion for graph: {}",
                                    currFrame->graph()->name()));
                            // 交换孪生帧
                            currFrame = twinFrame;
                            twinFrame = lastFrame;
                        } else {
                            // 不可复用栈帧的尾调用
                            if (twinFrame != nullptr && twinFrame != rootFrame) {
                                // 如果孪生帧不为空，覆写前需要先释放
                                // 如果孪生帧刚好指向根帧，不能先释放
                                framePool_.release(twinFrame);
                            }
                            // 将当前栈帧设置为孪生帧
                            twinFrame = currFrame;

                            // 创建新的栈帧并设置参数
                            Frame *funcFrame = framePool_.acquire(funcGraph);
                            fillFrameForFunc(lastFrame, funcFrame, funcGraph, n);

                            // 切换到新栈帧
                            currFrame = funcFrame;

                            // 跳过后续字节码，进行下一轮循环
                            goto loop_start;
                        }
                    }

                    // 自递归和互递归的情况在这里集中设置参数
                    fillFrameForFunc(lastFrame, currFrame, funcGraph, n);

                    // 跳过后续字节码，进行下一轮循环
                    goto loop_start;
                }

                Frame *funcFrame = framePool_.acquire(funcGraph);
                fillFrameForFunc(currFrame, funcFrame, funcGraph, n);
                slot_t callResult = call(funcGraph, funcFrame);

                currFrame->set(n->index(), callResult);
            } break;

            case NodeType::OPER: {
                auto opNode = tt::as_ptr<OperNode>(n);

                operator_t opFunc = opNode->getCachedOp();
                if (!opFunc) {
                    const auto &uri = opNode->oper()->uri();
                    auto found      = context_->execMgr().find(uri);
                    if (found) {
                        opFunc = *found;
                        opNode->setCachedOp(opFunc);
                    } else {
                        if (uri.starts_with(":mark/")) {
                            evalMarkedOperator(uri.substr(6), n, *currFrame);
                            break;
                        }
                        throwRuntimeFault(RuntimeDiag::UnrecognizedOperatorURI, uri);
                    }
                }

                operIndices_.clear();
                for (const auto &in : n->normInputs())
                    operIndices_.push_back(in->index());
                size_t normCnt = operIndices_.size();
                for (const auto &in : n->withInputs())
                    operIndices_.push_back(in->index());

                data_arr_t nargs{operIndices_.data(), normCnt};
                data_arr_t wargs{operIndices_.data() + normCnt, operIndices_.size() - normCnt};
                FrameArgsView withView(*currFrame, wargs);
                FrameArgsView normView(*currFrame, nargs);

                slot_t result = (*opFunc)(withView, normView, *context_);

                currFrame->set(n->index(), result);
            } break;

            case NodeType::EXIT:
                [[fallthrough]];
            case NodeType::PORT:
                [[fallthrough]];
            case NodeType::DATA:
                [[fallthrough]];
            case NodeType::SYNC:
                [[fallthrough]];
            case NodeType::GATE:
                break;

            default: {
                ASSERT(
                    false,
                    std::format(
                        "Node type {} should not appear in NodeVM execution.",
                        to_string(n->type())));
            } break;
            }

            EXEC_WHEN_DEBUG(
                GetDefaultLogger()
                    .in("NodeVM")
                    .debug("Executed node [{}/{}]: {}", i + 1, currNodes.size(), n->toString()));
        }
    }

        currRecursionDepth_--;

        Node *exitNode    = currGraph->exitNode();
        const auto &input = exitNode->normInputs();
        result = input.empty() ? NullSlot : currFrame->get<slot_t>(input.front()->index());

        // 按约定顺序释放栈帧（见文件顶部三种情形说明）
        if (twinFrame != nullptr) {
            // 说明已经触发了相互尾调用，要把孪生帧也释放掉
            // 相互尾调用优化时，currFrame 和 twinFrame 会互相切换
            // 把与传入的 rootFrame 不相等的那个先释放掉即可
            if (currFrame != rootFrame) {
                // 1. 若 curr 不是 root，先释放 curr（情形二、三中 curr 可能是中途尾调用的帧）
                framePool_.release(currFrame);
            }
            if (twinFrame != rootFrame) {
                // 2. 若存在孪生帧且孪生帧不是 root，释放孪生帧（互尾递归时缓存的另一图帧）
                framePool_.release(twinFrame);
            }
        }
        // 3. 最后释放根帧（call 拥有 rootFrame 的所有权）
        framePool_.release(rootFrame);

        return result;
    } catch (const RuntimeFault &fault) {
        currRecursionDepth_--;
        if (currFrame && currFrame != rootFrame) {
            framePool_.release(currFrame);
        }
        if (twinFrame && twinFrame != rootFrame && twinFrame != currFrame) {
            framePool_.release(twinFrame);
        }
        if (rootFrame) {
            framePool_.release(rootFrame);
        }
        auto sourceContext = context_ ? context_->sourceContext() : nullptr;
        if (currentNode) {
            throw reportRuntimeFault(
                *context_,
                fault,
                makeNodeExecutionSite(sourceContext, currGraph, currentNode, currRecursionDepth_));
        }
        throw reportRuntimeFault(
            *context_,
            fault,
            makeGraphExecutionSite(sourceContext, currGraph, currRecursionDepth_));
    } catch (Diagnostic &) {
        currRecursionDepth_--;
        if (currFrame && currFrame != rootFrame) {
            framePool_.release(currFrame);
        }
        if (twinFrame && twinFrame != rootFrame && twinFrame != currFrame) {
            framePool_.release(twinFrame);
        }
        if (rootFrame) {
            framePool_.release(rootFrame);
        }
        throw;
    }
}

GIR::graph_ptr_t NodeVMSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    if (!graph->hasOutput()) {
        throw reportRuntimeFault(
            *context_,
            RuntimeFault::make(RuntimeDiag::MissingMainFunction, context_->mainModule()->name()),
            makeGraphExecutionSite(context_->sourceContext(), graph.get()));
    }

    Frame *rootFrame = framePool_.acquire(graph.get());
    slot_t result =
        call(graph.get(), rootFrame); // call 内部负责释放 rootFrame（及可能产生的 curr/twin）
    context_->captureProcessExitCode(graph.get(), result);
    return Graph::null();
}

void NodeVMSchedPass::evalMarkedOperator(const std::string &uri, Node *node, Frame &currFrame) {
    if (uri == "map_arr") {
        evalMarkedOperator_map_arr(node, currFrame);
    } else if (uri == "apply_arr") {
        evalMarkedOperator_apply_arr(node, currFrame);
    } else if (uri == "filter_arr") {
        evalMarkedOperator_filter_arr(node, currFrame);
    } else if (uri == "reduce_arr" || uri == "unordered_reduce_arr") {
        evalMarkedOperator_reduce_arr(node, currFrame);
    } else if (uri == "foreach_arr" || uri == "unordered_foreach_arr") {
        evalMarkedOperator_foreach_arr(node, currFrame);
    } else {
        ASSERT(false, std::format("Mark operator {} not implemented in NodeVM.", uri));
    }
}

void NodeVMSchedPass::evalMarkedOperator_map_arr(Node *node, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(node->normInputs().front()->index());
    Function *func = currFrame.get<Function *>(node->withInputs().front()->index());
    Tuple *closure = func->tuple();

    Array *res   = Array::create(mm::autoSpace(), arr->size());
    slot_t *from = arr->data();
    slot_t *to   = res->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());
        frame->set(1, from[i]);
        for (size_t j = 0; j < closure->size(); ++j)
            frame->set(j + 2, closure->get<slot_t>(j));
        to[i] = call(func->graph(), frame);
    }
    currFrame.set(node->index(), res);
}

void NodeVMSchedPass::evalMarkedOperator_apply_arr(Node *node, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(node->normInputs().front()->index());
    Function *func = currFrame.get<Function *>(node->withInputs().front()->index());
    Tuple *closure = func->tuple();
    slot_t *data   = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());
        frame->set(1, data[i]);
        for (size_t j = 0; j < closure->size(); ++j)
            frame->set(j + 2, closure->get<slot_t>(j));
        data[i] = call(func->graph(), frame);
    }
    currFrame.set(node->index(), arr);
}

void NodeVMSchedPass::evalMarkedOperator_filter_arr(Node *node, Frame &currFrame) {
    Array *arr      = currFrame.get<Array *>(node->normInputs().front()->index());
    Function *func  = currFrame.get<Function *>(node->withInputs().front()->index());
    Tuple *closure  = func->tuple();
    Array *filtered = Array::create(mm::autoSpace(), arr->size());
    slot_t *from    = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());
        frame->set(1, from[i]);
        for (size_t j = 0; j < closure->size(); ++j)
            frame->set(j + 2, closure->get<slot_t>(j));
        slot_t result = call(func->graph(), frame);
        if (fromSlot<bool>(result))
            filtered->append(from[i]);
    }
    filtered->shrinkToFit();
    currFrame.set(node->index(), filtered);
}

void NodeVMSchedPass::evalMarkedOperator_reduce_arr(Node *node, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(node->normInputs().front()->index());
    Function *func = currFrame.get<Function *>(node->withInputs()[0]->index());
    slot_t init    = currFrame.get<slot_t>(node->withInputs()[1]->index());
    Tuple *closure = func->tuple();

    if (arr->size() == 0) {
        currFrame.set(node->index(), init);
        return;
    }
    slot_t acc   = init;
    slot_t *from = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());
        frame->set(1, acc);
        frame->set(2, from[i]);
        for (size_t j = 0; j < closure->size(); ++j)
            frame->set(j + 3, closure->get<slot_t>(j));
        acc = call(func->graph(), frame);
    }
    currFrame.set(node->index(), acc);
}

void NodeVMSchedPass::evalMarkedOperator_foreach_arr(Node *node, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(node->normInputs().front()->index());
    Function *func = currFrame.get<Function *>(node->withInputs().front()->index());
    Tuple *closure = func->tuple();
    slot_t *from   = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());
        frame->set(1, from[i]);
        for (size_t j = 0; j < closure->size(); ++j)
            frame->set(j + 2, closure->get<slot_t>(j));
        call(func->graph(), frame);
    }
    currFrame.set(node->index(), NullSlot);
}
