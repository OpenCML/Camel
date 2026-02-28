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
 * Updated: Feb. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "nodevm.h"
#include "camel/common/algo/topo.h"
#include "camel/compile/gir/nodes.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/module/module.h"
#include "camel/core/operator.h"

#include <stack>

using namespace std;
using namespace GraphIR;

std::shared_ptr<node_vec_t> NodeVMSchedPass::getTopoNodes(Graph *graph) {
    ASSERT(
        !graph->dirty(),
        std::format("Graph {} is dirty, please rearrange before executing.", graph->name()));

    if (graphTopoNodesCache_.find(graph) == graphTopoNodesCache_.end()) {
        node_ptr_t exitNode = graph->exitNode();
        auto sortedNodes    = findReachable(
            exitNode,
            [](const node_ptr_t &n) {
                node_vec_t ins;
                ins.reserve(n->dataInputs().size() + n->ctrlInputs().size());
                for (const auto &in : n->ctrlInputs()) {
                    if (&in->graph() == &n->graph()) // only consider nodes in the same graph
                        ins.push_back(in);
                }
                // Put value computation nodes at the back for correct tail-call optimization
                for (const auto &in : n->dataInputs()) {
                    if (&in->graph() == &n->graph()) // only consider nodes in the same graph
                        ins.push_back(in);
                }
                return ins;
            },
            true // skip the start node itself
        );

        EXEC_WHEN_DEBUG([&]() {
            GetDefaultLogger().in("Topo").debug(
                "Topologically sorted nodes for graph {}:",
                graph->name());
            for (const auto &n : sortedNodes) {
                GetDefaultLogger().in("Topo").debug("  {}", n->toString());
            }
            size_t totalNodeCnt =
                graph->nodes().size() + graph->ports().size() + graph->closure().size();
            if (sortedNodes.size() != totalNodeCnt) {
                GraphIR::node_vec_t unreachableNodes;
                for (const auto &n : graph->nodes()) {
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
        }());

        const auto &sortedNodesPtr  = std::make_shared<node_vec_t>(std::move(sortedNodes));
        graphTopoNodesCache_[graph] = sortedNodesPtr;
        return sortedNodesPtr;
    }
    return graphTopoNodesCache_[graph];
}

slot_t NodeVMSchedPass::doCall(const node_ptr_t &n, Frame *currFrame) {
    Graph *targetGraph = nullptr;
    node_vec_t argNodes;
    node_vec_t closureNodes;

    if (n->type() == NodeType::CALL) {
        Function *func = currFrame->get<Function *>(n->withInputs().front()->index());
        targetGraph    = func->graph();
        argNodes       = n->normInputs();
        closureNodes   = targetGraph->closure();
    } else {
        ASSERT(n->type() == NodeType::FUNC, "doCall expects CALL or FUNC node");
        auto funcNode = tt::as_shared<FuncNode>(n);
        targetGraph   = &funcNode->func()->graph();
        argNodes      = n->normInputs();
        closureNodes  = targetGraph->closure();
    }

    Frame *funcFrame      = framePool_.acquire(targetGraph);
    const auto &portNodes = targetGraph->ports();

    size_t i = 0;
    for (; i < argNodes.size() && i < portNodes.size(); ++i) {
        funcFrame->set(portNodes[i]->index(), currFrame->get<slot_t>(argNodes[i]->index()));
    }

    if (n->type() == NodeType::CALL) {
        Function *func = currFrame->get<Function *>(n->withInputs().front()->index());
        Tuple *closure = func->tuple();
        for (size_t j = 0; j < closure->size() && j < closureNodes.size(); ++j) {
            funcFrame->set(closureNodes[j]->index(), closure->get<slot_t>(j));
        }
    } else {
        for (size_t j = 0; j < n->withInputs().size() && j < closureNodes.size(); ++j) {
            funcFrame->set(
                closureNodes[j]->index(),
                currFrame->get<slot_t>(n->withInputs()[j]->index()));
        }
    }

    slot_t result = call(targetGraph, funcFrame);
    framePool_.release(funcFrame);
    return result;
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
//     root1              root2  （C 的帧在 doCall 内 acquire/release）
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
static void
fillFrameForCall(Frame *dest, Graph *targetGraph, const GraphIR::node_ptr_t &n, Frame *source) {
    using namespace GraphIR;
    node_vec_t argNodes;
    node_vec_t closureNodes = targetGraph->closure();
    node_vec_t portNodes    = targetGraph->ports();

    if (n->type() == NodeType::CALL) {
        argNodes = n->normInputs();
        size_t i = 0;
        for (; i < argNodes.size() && i < portNodes.size(); ++i)
            dest->set(portNodes[i]->index(), source->get<slot_t>(argNodes[i]->index()));
        Function *func = source->get<Function *>(n->withInputs().front()->index());
        Tuple *closure = func->tuple();
        for (size_t j = 0; j < closure->size() && j < closureNodes.size(); ++j)
            dest->set(closureNodes[j]->index(), closure->get<slot_t>(j));
    } else {
        argNodes = n->normInputs();
        size_t i = 0;
        for (; i < argNodes.size() && i < portNodes.size(); ++i)
            dest->set(portNodes[i]->index(), source->get<slot_t>(argNodes[i]->index()));
        for (size_t j = 0; j < n->withInputs().size() && j < closureNodes.size(); ++j)
            dest->set(closureNodes[j]->index(), source->get<slot_t>(n->withInputs()[j]->index()));
    }
}

slot_t NodeVMSchedPass::call(Graph *graph, Frame *rootFrame) {
    currRecursionDepth_++;
    if (currRecursionDepth_ > maxRecursionDepth_) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MaxRecursionDepthExceeded)
            .commit(graph->name(), maxRecursionDepth_);
        currRecursionDepth_--;
        framePool_.release(rootFrame); // 提前返回时仍由 call 负责释放根帧
        return NullSlot;
    }

    // 尾调用优化：loop 为 true 时本层 C++ 栈帧不返回，继续用新图/新帧执行，避免栈溢出
    bool loop     = false;
    slot_t result = NullSlot;

    // currFrame：当前执行的栈帧；twinFrame：孪生帧，用于互尾递归（A 尾调 B、B 尾调 A）时复用
    Frame *currFrame = rootFrame;
    Frame *twinFrame = nullptr;
    Graph *currGraph = graph;
    std::stack<node_ptr_t> brInfoStack_;

    // 尾调用优化主循环：loop 为 true 时不退出 C++ 栈帧，用新的 currGraph/currFrame 继续执行
    do {
        loop              = false;
        auto nodesPtr     = getTopoNodes(currGraph);
        const auto &nodes = *nodesPtr;

        for (size_t i = 0; i < nodes.size(); ++i) {
            const node_ptr_t &n = nodes[i];

#ifndef NDEBUG
            if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                camel::DebugBreakpoint::Hit("gir_node", n.get());
#endif

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
                auto accsNode     = tt::as_shared<AccsNode>(n);
                data_idx_t srcIdx = n->dataInputs().front()->index();
                if (accsNode->isNum()) {
                    size_t idx = accsNode->index<size_t>();
                    Tuple *t   = currFrame->get<Tuple *>(srcIdx);
                    ASSERT(idx < t->size(), "Tuple index out of bounds in NodeVM.");
                    currFrame->set(n->index(), t->get<slot_t>(idx));
                } else {
                    std::string key  = accsNode->index<std::string>();
                    Struct *s        = currFrame->get<Struct *>(srcIdx);
                    Type *structType = currFrame->typeAt<Type>(srcIdx);
                    currFrame->set(n->index(), s->get<slot_t>(key, structType));
                }
            } break;

            case NodeType::BRCH: {
                const auto &normIns  = n->normInputs();
                const auto &withIns  = n->withInputs();
                const auto &ctrlOuts = n->ctrlOutputs();
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
                brInfoStack_.push(ctrlOuts[jumpIdx]);
                i += ctrlOuts.size();
                continue;
            } break;

            case NodeType::JOIN: {
                const auto &nargs                  = n->normInputs();
                [[maybe_unused]] const auto &wargs = n->withInputs();
                ASSERT(!nargs.empty(), "JOIN must have norm input (branch index).");
                [[maybe_unused]] int32_t brIndex = currFrame->get<int32_t>(nargs.front()->index());
                ASSERT(
                    brIndex >= 0 && static_cast<size_t>(brIndex) < wargs.size(),
                    "JOIN branch index out of range in NodeVM.");
                node_ptr_t execNode = brInfoStack_.top();
                brInfoStack_.pop();

                // 尾调用判定：当前节点是图的最后一个节点且为图的返回值产生节点（outputNode）
                bool isTailCall =
                    (i == nodes.size() - 1) && (currGraph->outputNode().get() == n.get());
                if (isTailCall) {
                    Graph *targetGraph = nullptr;
                    if (execNode->type() == NodeType::CALL) {
                        Function *func =
                            currFrame->get<Function *>(execNode->withInputs().front()->index());
                        targetGraph = func->graph();
                    } else {
                        targetGraph = &tt::as_shared<FuncNode>(execNode)->func()->graph();
                    }
                    Frame *lastFrame = currFrame;

                    if (targetGraph == currGraph) {
                        // 自递归尾调用：目标图与当前图相同，直接复用当前帧，仅重写参数
                        fillFrameForCall(currFrame, targetGraph, execNode, lastFrame);
                    } else {
                        // 跨图尾调用：需切换帧；可能为互尾递归（A↔B）或尾调到第三方（如 C）
                        if (twinFrame && twinFrame->graph() == targetGraph) {
                            // 互尾递归：孪生帧恰好是目标图的帧，直接复用，交换 curr 与 twin
                            currFrame = twinFrame;
                            fillFrameForCall(currFrame, targetGraph, execNode, lastFrame);
                            twinFrame = lastFrame;
                        } else {
                            // 非互尾：需释放旧孪生帧（若存在且非
                            // root），将当前帧记为孪生帧，申请目标帧
                            if (twinFrame != nullptr && twinFrame != rootFrame)
                                framePool_.release(twinFrame);
                            twinFrame = lastFrame;
                            currFrame = framePool_.acquire(targetGraph);
                            fillFrameForCall(currFrame, targetGraph, execNode, lastFrame);
                        }
                        currGraph = targetGraph;
                    }
                    loop = true;
                    break;
                }
                slot_t callResult = doCall(execNode, currFrame);
                currFrame->set(n->index(), callResult);
            } break;

            case NodeType::CALL: {
                bool isTailCall =
                    (i == nodes.size() - 1) && (currGraph->outputNode().get() == n.get());
                if (isTailCall) {
                    // 尾调用：不递归 call，而是切换 currGraph/currFrame 并重入 do 循环
                    Graph *targetGraph =
                        currFrame->get<Function *>(n->withInputs().front()->index())->graph();
                    Frame *lastFrame = currFrame;
                    if (targetGraph == currGraph) {
                        fillFrameForCall(currFrame, targetGraph, n, lastFrame);
                    } else {
                        if (twinFrame && twinFrame->graph() == targetGraph) {
                            currFrame = twinFrame;
                            fillFrameForCall(currFrame, targetGraph, n, lastFrame);
                            twinFrame = lastFrame;
                        } else {
                            if (twinFrame != nullptr && twinFrame != rootFrame)
                                framePool_.release(twinFrame);
                            twinFrame = lastFrame;
                            currFrame = framePool_.acquire(targetGraph);
                            fillFrameForCall(currFrame, targetGraph, n, lastFrame);
                        }
                        currGraph = targetGraph;
                    }
                    loop = true;
                    break;
                }
                slot_t callResult = doCall(n, currFrame);
                currFrame->set(n->index(), callResult);
            } break;

            case NodeType::FUNC: {
                bool isTailCall =
                    (i == nodes.size() - 1) && (currGraph->outputNode().get() == n.get());
                if (isTailCall) {
                    // 尾调用：与 CALL 相同，自递归复用帧，跨图则复用孪生帧或申请新帧
                    Graph *targetGraph = &tt::as_shared<FuncNode>(n)->func()->graph();
                    Frame *lastFrame   = currFrame;
                    if (targetGraph == currGraph) {
                        fillFrameForCall(currFrame, targetGraph, n, lastFrame);
                    } else {
                        if (twinFrame && twinFrame->graph() == targetGraph) {
                            currFrame = twinFrame;
                            fillFrameForCall(currFrame, targetGraph, n, lastFrame);
                            twinFrame = lastFrame;
                        } else {
                            if (twinFrame != nullptr && twinFrame != rootFrame)
                                framePool_.release(twinFrame);
                            twinFrame = lastFrame;
                            currFrame = framePool_.acquire(targetGraph);
                            fillFrameForCall(currFrame, targetGraph, n, lastFrame);
                        }
                        currGraph = targetGraph;
                    }
                    loop = true;
                    break;
                }
                slot_t callResult = doCall(n, currFrame);
                currFrame->set(n->index(), callResult);
            } break;

            case NodeType::OPER: {
                auto opNode     = tt::as_shared<OperNode>(n);
                std::string uri = opNode->oper()->uri();

                if (uri.starts_with(":mark/")) {
                    evalMarkedOperator(uri.substr(6), n, *currFrame);
                    break;
                }

                auto opFunc = context_->execMgr().find(uri);
                if (!opFunc) {
                    context_->rtmDiags()->of(RuntimeDiag::UnrecognizedOperatorURI).commit(uri);
                }
                std::vector<data_idx_t> normIndices, withIndices;
                for (const auto &in : n->normInputs())
                    normIndices.push_back(in->index());
                for (const auto &in : n->withInputs())
                    withIndices.push_back(in->index());
                data_arr_t nargs{normIndices.data(), normIndices.size()};
                data_arr_t wargs{withIndices.data(), withIndices.size()};
                FrameArgsView withView(*currFrame, wargs);
                FrameArgsView normView(*currFrame, nargs);
                slot_t result = (*opFunc)(withView, normView, *context_);
                currFrame->set(n->index(), result);
            } break;

            case NodeType::EXIT: {
                // exit 节点在 topo 中被 skip，通常不会执行到此
                const auto &input = n->normInputs();
                result = input.empty() ? NullSlot : currFrame->get<slot_t>(input.front()->index());
                break;
            } break;

            case NodeType::DATA:
                [[fallthrough]];
            case NodeType::PORT:
                [[fallthrough]];
            case NodeType::SYNC:
                [[fallthrough]];
            case NodeType::NREF:
                break;

            default: {
                ASSERT(
                    false,
                    std::format(
                        "Node type {} should not appear in NodeVM execution.",
                        to_string(n->type())));
            } break;
            }
        }

        if (!loop) {
            const node_ptr_t &exitNode = currGraph->exitNode();
            const auto &input          = exitNode->normInputs();
            result = input.empty() ? NullSlot : currFrame->get<slot_t>(input.front()->index());
            break;
        }
    } while (true);

    currRecursionDepth_--;

    // 按约定顺序释放栈帧（见文件顶部三种情形说明）
    // 1. 若 curr 不是 root，先释放 curr（情形二、三中 curr 可能是中途尾调用的帧）
    if (currFrame != rootFrame)
        framePool_.release(currFrame);
    // 2. 若存在孪生帧且孪生帧不是 root，释放孪生帧（互尾递归时缓存的另一图帧）
    if (twinFrame != nullptr && twinFrame != rootFrame)
        framePool_.release(twinFrame);
    // 3. 最后释放根帧（call 拥有 rootFrame 的所有权）
    framePool_.release(rootFrame);

    return result;
}

GraphIR::graph_ptr_t NodeVMSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    if (!graph->hasOutput()) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MissingMainFunction)
            .commit(context_->mainModule()->name());
    }

    Frame *rootFrame = framePool_.acquire(graph.get());
    call(graph.get(), rootFrame); // call 内部负责释放 rootFrame（及可能产生的 curr/twin）
    return Graph::null();
}

void NodeVMSchedPass::evalMarkedOperator(
    const std::string &uri, const node_ptr_t &node, Frame &currFrame) {
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

void NodeVMSchedPass::evalMarkedOperator_map_arr(const node_ptr_t &node, Frame &currFrame) {
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
        framePool_.release(frame);
    }
    currFrame.set(node->index(), res);
}

void NodeVMSchedPass::evalMarkedOperator_apply_arr(const node_ptr_t &node, Frame &currFrame) {
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
        framePool_.release(frame);
    }
    currFrame.set(node->index(), arr);
}

void NodeVMSchedPass::evalMarkedOperator_filter_arr(const node_ptr_t &node, Frame &currFrame) {
    Array *arr             = currFrame.get<Array *>(node->normInputs().front()->index());
    Function *func         = currFrame.get<Function *>(node->withInputs().front()->index());
    Tuple *closure         = func->tuple();
    const auto &retArrType = currFrame.typeAt<ArrayType>(node->index());
    Array *filtered        = Array::create(mm::autoSpace(), arr->size());
    slot_t *from           = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());
        frame->set(1, from[i]);
        for (size_t j = 0; j < closure->size(); ++j)
            frame->set(j + 2, closure->get<slot_t>(j));
        slot_t result = call(func->graph(), frame);
        framePool_.release(frame);
        if (fromSlot<bool>(result))
            filtered->append(from[i], retArrType);
    }
    filtered->shrinkToFit(retArrType);
    currFrame.set(node->index(), filtered);
}

void NodeVMSchedPass::evalMarkedOperator_reduce_arr(const node_ptr_t &node, Frame &currFrame) {
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
        framePool_.release(frame);
    }
    currFrame.set(node->index(), acc);
}

void NodeVMSchedPass::evalMarkedOperator_foreach_arr(const node_ptr_t &node, Frame &currFrame) {
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
        framePool_.release(frame);
    }
    currFrame.set(node->index(), NullSlot);
}
