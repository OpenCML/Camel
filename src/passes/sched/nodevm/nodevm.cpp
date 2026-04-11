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
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "nodevm.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/global_config.h"
#include "camel/core/module/module.h"
#include "camel/core/operator.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"

#include <span>

using namespace std;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using namespace camel::core::error;
using camel::compile::gir::data_idx_t;

namespace {

constexpr size_t kNodeVmCacheSlot = 0;

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCAccsBody;
using camel::runtime::GCBrchBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::kInvalidNodeRef;

inline NodeVMGraphCache *nodeVmCacheOf(camel::runtime::GCGraph *graph) {
    return graph ? reinterpret_cast<NodeVMGraphCache *>(graph->extraSlot(kNodeVmCacheSlot))
                 : nullptr;
}

inline void setNodeVmCacheOf(camel::runtime::GCGraph *graph, NodeVMGraphCache *cache) {
    if (graph) {
        graph->setExtraSlot(kNodeVmCacheSlot, reinterpret_cast<uintptr_t>(cache));
    }
}

inline data_idx_t dataIndexOf(const GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "NodeVM runtime node lookup resolved to null.");
    return node->dataIndex;
}

} // namespace

NodeVMSchedPass::~NodeVMSchedPass() = default;

std::span<const gc_node_ref_t>
NodeVMSchedPass::buildTopoNodes(camel::runtime::GCGraph *runtimeGraph) {
    ASSERT(runtimeGraph != nullptr, "NodeVM runtime graph is null.");
    auto sortedNodeRefs = camel::execute::buildReachableExecutionTopoIndices(runtimeGraph);

    EXEC_WHEN_DEBUG({
        CAMEL_LOG_DEBUG_S("Topo", "Topologically sorted nodes for graph {}:", runtimeGraph->name());
        for (gc_node_ref_t nodeRef : sortedNodeRefs) {
            const auto *node = runtimeGraph->node(nodeRef);
            CAMEL_LOG_DEBUG_S(
                "Topo",
                "  ref={} kind={} slot={}",
                nodeRef,
                static_cast<int>(node ? node->kind : GCNodeKind::Data),
                node ? node->dataIndex : 0);
        }
    });

    auto cache          = std::make_unique<NodeVMGraphCache>();
    cache->topoNodeRefs = std::move(sortedNodeRefs);
    cache->directCallTargets.resize(cache->topoNodeRefs.size(), nullptr);
    for (size_t idx = 0; idx < cache->topoNodeRefs.size(); ++idx) {
        const auto *node = runtimeGraph->node(cache->topoNodeRefs[idx]);
        if (!node || node->kind != GCNodeKind::Func) {
            continue;
        }
        cache->directCallTargets[idx] = runtimeGraph->directCalleeGraphOf(cache->topoNodeRefs[idx]);
        ASSERT(
            cache->directCallTargets[idx] != nullptr,
            "NodeVM direct FUNC target must be materialized into runtime metadata.");
    }
    NodeVMGraphCache *cacheRaw = cache.get();
    graphCaches_.push_back(std::move(cache));
    setNodeVmCacheOf(runtimeGraph, cacheRaw);
    return std::span<const gc_node_ref_t>(cacheRaw->topoNodeRefs);
}

std::span<const gc_node_ref_t>
NodeVMSchedPass::topoNodesFor(camel::runtime::GCGraph *runtimeGraph) {
    ASSERT(runtimeGraph != nullptr, "NodeVM graph must be materialized before topo lookup.");
    if (auto *cache = nodeVmCacheOf(runtimeGraph)) {
        if (!cache->topoNodeRefs.empty()) {
            return std::span<const gc_node_ref_t>(cache->topoNodeRefs);
        }
    }
    return buildTopoNodes(runtimeGraph);
}

// =============================================================================
// Tail-call optimization: frame lifetime for mutually recursive calls that may
// also invoke a third graph.
// =============================================================================
//
// Let A be the root frame. A and B may tail-call each other, and either may
// also call C in the middle.
//
// Case 1: A or B performs a normal call into C. C's frame is released when the
// call returns.
//
// Case 2: A tail-calls C while twin points at B. B must be released first
// before allocating C's frame.
//
// Case 3: B tail-calls C while twin points at A. The root frame A cannot be
// released early, so cleanup happens when the C++ stack frame exits.
//
// Release order on exit:
//   1. Release curr if it is not root.
//   2. Release twin if it exists and is not root.
//   3. Release root last. The call owns rootFrame and is responsible for it.
// =============================================================================

// Execute one runtime graph call. Arguments for CALL/FUNC are copied from the
// source frame into the callee frame's ports and closure slots.
slot_t NodeVMSchedPass::call(camel::runtime::GCGraph *rootRuntimeGraph, Frame *rootFrame) {
    currRecursionDepth_++;
    slot_t result = NullSlot;
    ASSERT(
        rootRuntimeGraph != nullptr,
        "NodeVM runtime graph must be materialized before execution.");

    Frame *currFrame       = rootFrame;
    Frame *twinFrame       = nullptr;
    auto *currRuntimeGraph = rootRuntimeGraph;
    try {
        if (currRecursionDepth_ > maxRecursionDepth_) {
            throwRuntimeFault(
                RuntimeDiag::MaxRecursionDepthExceeded,
                rootRuntimeGraph->name(),
                maxRecursionDepth_);
        }

        std::span<const gc_node_ref_t> currNodes = topoNodesFor(currRuntimeGraph);

        // Branch execution is implemented by skipping until the selected arm
        // head, executing through the arm tail, and then resuming at JOIN.
        gc_node_ref_t tillNode = kInvalidNodeRef;
        gc_node_ref_t skipNode = kInvalidNodeRef;
        gc_node_ref_t joinNode = kInvalidNodeRef;

    // Tail-call loop. Rebind currRuntimeGraph/currFrame instead of growing the C++ stack.
    loop_start: {
        const size_t nodesSize       = currNodes.size();
        const gc_node_ref_t lastNode = camel::execute::resolveRuntimeTailValueRef(currRuntimeGraph);
        const bool lastNodeIsJoin    = lastNode != kInvalidNodeRef &&
                                       currRuntimeGraph->node(lastNode)->kind == GCNodeKind::Join;

        size_t i = 0;
        for (; i < nodesSize; ++i) {
            const gc_node_ref_t nodeRef = currNodes[i];
            const GCNode *n             = currRuntimeGraph->node(nodeRef);
            ASSERT(n != nullptr, "NodeVM execution resolved to a null runtime node.");

            if (tillNode != kInvalidNodeRef) {
                if (tillNode == nodeRef) {
                    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                        "NodeVM",
                        "Reached tillNode [{}/{}] graph={}: ref={}",
                        i + 1,
                        currNodes.size(),
                        currRuntimeGraph->name(),
                        nodeRef));
                    tillNode = kInvalidNodeRef;
                } else {
                    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                        "NodeVM",
                        "Skipping node [{}/{}] graph={}: ref={}",
                        i + 1,
                        currNodes.size(),
                        currRuntimeGraph->name(),
                        nodeRef));
                    continue;
                }
            }
            if (skipNode != kInvalidNodeRef && skipNode == nodeRef) {
                EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                    "NodeVM",
                    "Reached skipNode [{}/{}] graph={}: ref={}",
                    i + 1,
                    currNodes.size(),
                    currRuntimeGraph->name(),
                    nodeRef));
                skipNode = kInvalidNodeRef;
                tillNode = joinNode;
            }

            EXEC_WHEN_DEBUG({
                CAMEL_LOG_DEBUG_S(
                    "NodeVM",
                    "Executing node [{}/{}] graph={}: ref={} kind={} slot={}",
                    i + 1,
                    currNodes.size(),
                    currRuntimeGraph->name(),
                    nodeRef,
                    static_cast<int>(n->kind),
                    n->dataIndex);
            });
            switch (n->kind) {
            case GCNodeKind::Cast: {
                const auto normInputs = currRuntimeGraph->normInputsOf(nodeRef);
                ASSERT(!normInputs.empty(), "CAST node must have one norm input.");
                const auto srcIdx = dataIndexOf(currRuntimeGraph, normInputs.front());
                Type *srcType     = currFrame->typeAt<Type>(srcIdx);
                Type *tgtType     = n->dataType;
                slot_t value      = currFrame->get<slot_t>(srcIdx);
                slot_t result     = tgtType->castSlotFrom(value, srcType);
                currFrame->set(n->dataIndex, result);
            } break;

            case GCNodeKind::Copy: {
                const auto normInputs = currRuntimeGraph->normInputsOf(nodeRef);
                ASSERT(!normInputs.empty(), "COPY node must have one norm input.");
                data_idx_t srcIdx = dataIndexOf(currRuntimeGraph, normInputs.front());
                TypeCode srcCode  = currFrame->codeAt(srcIdx);
                if (isGCTraced(srcCode)) {
                    Object *srcData  = currFrame->get<Object *>(srcIdx);
                    Type *srcTypePtr = currFrame->typeAt<Type>(srcIdx);
                    currFrame->set(
                        n->dataIndex,
                        srcData->clone(mm::autoSpace(), srcTypePtr, false));
                } else {
                    currFrame->set(n->dataIndex, currFrame->get<slot_t>(srcIdx));
                }
            } break;

            case GCNodeKind::Fill: {
                const auto normInputs = currRuntimeGraph->normInputsOf(nodeRef);
                const auto dataInputs = currRuntimeGraph->withInputsOf(nodeRef);
                ASSERT(!normInputs.empty(), "FILL node must have one source input.");
                const auto srcIdx = dataIndexOf(currRuntimeGraph, normInputs.front());
                TypeCode srcCode  = currFrame->codeAt(srcIdx);
                Type *srcType     = currFrame->typeAt<Type>(srcIdx);
                ASSERT(isGCTraced(srcCode), "FILL target type is not GC-traced in NodeVM.");
                Object *srcObj =
                    currFrame->get<Object *>(srcIdx)->clone(mm::autoSpace(), srcType, false);
                ASSERT(srcObj != nullptr, "FILL target data is null.");

                switch (srcCode) {
                case TypeCode::Tuple: {
                    auto type          = tt::as_ptr<TupleType>(srcType);
                    auto tup           = tt::as_ptr<Tuple>(srcObj);
                    const size_t *refs = type->refs();
                    for (size_t j = 0; j < dataInputs.size(); ++j) {
                        tup->set<slot_t>(
                            refs[j],
                            currFrame->get<slot_t>(dataIndexOf(currRuntimeGraph, dataInputs[j])));
                    }
                } break;
                case TypeCode::Array: {
                    auto arr = tt::as_ptr<Array>(srcObj);
                    for (size_t j = 0; j < dataInputs.size(); ++j) {
                        arr->set<slot_t>(
                            j,
                            currFrame->get<slot_t>(dataIndexOf(currRuntimeGraph, dataInputs[j])));
                    }
                } break;
                case TypeCode::Struct: {
                    auto type          = tt::as_ptr<StructType>(srcType);
                    auto str           = tt::as_ptr<Struct>(srcObj);
                    const size_t *refs = type->refs();
                    for (size_t j = 0; j < dataInputs.size(); ++j) {
                        str->set<slot_t>(
                            refs[j],
                            currFrame->get<slot_t>(dataIndexOf(currRuntimeGraph, dataInputs[j])));
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
                        closureData->set<slot_t>(
                            j,
                            currFrame->get<slot_t>(dataIndexOf(currRuntimeGraph, dataInputs[j])));
                    }
                } break;
                default:
                    ASSERT(
                        false,
                        std::format(
                            "Unsupported FILL target type {} in NodeVM.",
                            typeCodeToString(srcCode)));
                }
                currFrame->set(n->dataIndex, srcObj);
            } break;

            case GCNodeKind::Accs: {
                const auto normInputs = currRuntimeGraph->normInputsOf(nodeRef);
                ASSERT(!normInputs.empty(), "ACCS node must have one source input.");
                data_idx_t srcIdx = dataIndexOf(currRuntimeGraph, normInputs.front());
                const auto *body  = currRuntimeGraph->nodeBodyAs<GCAccsBody>(nodeRef);
                if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
                    size_t idx = body->value;
                    Tuple *t   = currFrame->get<Tuple *>(srcIdx);
                    ASSERT(idx < t->size(), "Tuple index out of bounds in NodeVM.");
                    currFrame->set(n->dataIndex, t->get<slot_t>(idx));
                } else {
                    std::string key  = std::string(body->key());
                    Struct *s        = currFrame->get<Struct *>(srcIdx);
                    Type *structType = currFrame->typeAt<Type>(srcIdx);
                    currFrame->set(n->dataIndex, s->get<slot_t>(key, structType));
                }
            } break;

            case GCNodeKind::Brch: {
                const size_t jumpIdx =
                    camel::execute::selectRuntimeBranchArm(currRuntimeGraph, nodeRef, currFrame);
                currFrame->set(n->dataIndex, fromSlot<Int32>(static_cast<Int32>(jumpIdx)));

                const auto arms  = currRuntimeGraph->branchArmsOf(nodeRef);
                const auto *body = currRuntimeGraph->nodeBodyAs<GCBrchBody>(nodeRef);
                ASSERT(jumpIdx < arms.size(), "Branch arm index out of range in NodeVM.");
                tillNode = arms[jumpIdx].head;
                skipNode = arms[jumpIdx].tail;
                joinNode = body->join;
                EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                    "NodeVM",
                    "BRCH ref {}: jumpIdx={}, branches={}, tillNode={}, skipNode={}, joinNode={}",
                    nodeRef,
                    jumpIdx,
                    arms.size(),
                    tillNode,
                    skipNode,
                    joinNode));
            } break;

            case GCNodeKind::Join: {
                const auto nargs = currRuntimeGraph->normInputsOf(nodeRef);
                const auto wargs = currRuntimeGraph->withInputsOf(nodeRef);
                ASSERT(!nargs.empty(), "JOIN must have norm input (branch index).");
                int32_t brIndex =
                    currFrame->get<int32_t>(dataIndexOf(currRuntimeGraph, nargs.front()));
                ASSERT(
                    brIndex >= 0 && static_cast<size_t>(brIndex) < wargs.size(),
                    "JOIN branch index out of range in NodeVM.");
                // The selected arm has already executed sequentially. Its tail
                // slot now holds the branch result consumed by JOIN.
                slot_t branchResult = currFrame->get<slot_t>(
                    dataIndexOf(currRuntimeGraph, wargs[static_cast<size_t>(brIndex)]));
                currFrame->set(n->dataIndex, branchResult);
            } break;

            case GCNodeKind::Call: {
                const auto withInputs = currRuntimeGraph->withInputsOf(nodeRef);
                ASSERT(!withInputs.empty(), "CALL node must have exactly one callee input");
                Function *func =
                    currFrame->get<Function *>(dataIndexOf(currRuntimeGraph, withInputs.front()));
                auto *funcRuntimeGraph = func->graph();
                ASSERT(
                    funcRuntimeGraph != nullptr,
                    "NodeVM indirect CALL requires a materialized runtime graph target.");

                Frame *funcFrame = framePool_.acquire(funcRuntimeGraph);
                camel::execute::fillFrameForIndirectCall(
                    currFrame,
                    funcFrame,
                    currRuntimeGraph,
                    nodeRef);

                slot_t callResult = call(funcRuntimeGraph, funcFrame);
                currFrame->set(n->dataIndex, callResult);
            } break;

            case GCNodeKind::Func: {
                auto *callerRuntimeGraph = currRuntimeGraph;
                auto *runtimeTarget      = nodeVmCacheOf(currRuntimeGraph)->directCallTargets[i];
                ASSERT(
                    runtimeTarget != nullptr,
                    "NodeVM direct FUNC target must have a materialized runtime graph.");

                // Tail-call optimization. See docs/technical/19_tail_call_optimization.md.
                size_t anchorIdx = nodesSize;
                for (size_t k = 0; k < nodesSize; ++k) {
                    if (currNodes[k] == lastNode) {
                        anchorIdx = k;
                        break;
                    }
                }
                const bool anchorOk =
                    anchorIdx < nodesSize && camel::execute::hasOnlyTrivialRuntimeTailSuffixAfter(
                                                 currRuntimeGraph,
                                                 currNodes,
                                                 anchorIdx);
                const bool tailShape = (nodeRef == lastNode) ||
                                       (lastNodeIsJoin && camel::execute::runtimeNodeOutputsContain(
                                                              currRuntimeGraph,
                                                              nodeRef,
                                                              lastNode));
                bool isTailCall      = anchorOk && tailShape;
                if (isTailCall) {
                    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                        "NodeVM",
                        "Optimizing tail-call for node [{}/{}] graph={}: ref={}",
                        i + 1,
                        currNodes.size(),
                        currRuntimeGraph->name(),
                        nodeRef));
                    // Reuse the current C++ stack frame by rebinding graph/frame state.
                    Frame *lastFrame = currFrame;
                    tillNode         = kInvalidNodeRef;
                    skipNode         = kInvalidNodeRef;

                    if (runtimeTarget == currRuntimeGraph) {
                        // Self recursion can keep both the current frame and node sequence.
                        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                            "NodeVM",
                            "Optimizing self-recursion for graph: {}",
                            currFrame->graph()->name()));
                    } else {
                        // Switch to the callee graph and its topo sequence.
                        currRuntimeGraph = runtimeTarget;
                        currNodes        = topoNodesFor(currRuntimeGraph);

                        // Mutual tail recursion is common because branches are
                        // compiled as subgraphs. Keep a twin frame and swap
                        // between A/B without growing the C++ stack.
                        if (twinFrame && twinFrame->runtimeGraph() == runtimeTarget) {
                            // Reuse the cached twin frame when it already matches the target.
                            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                                "NodeVM",
                                "Optimizing mutual-tail-recursion for graph: {}",
                                currFrame->graph()->name()));
                            currFrame = twinFrame;
                            twinFrame = lastFrame;
                        } else {
                            if (twinFrame != nullptr && twinFrame != rootFrame) {
                                framePool_.release(twinFrame);
                            }
                            twinFrame = currFrame;

                            Frame *funcFrame = framePool_.acquire(runtimeTarget);
                            camel::execute::fillFrameForDirectInvoke(
                                lastFrame,
                                funcFrame,
                                callerRuntimeGraph,
                                nodeRef);

                            currFrame = funcFrame;
                            goto loop_start;
                        }
                    }

                    // Self recursion and mutual recursion both land here to
                    // refresh the callee-visible argument slots.
                    camel::execute::fillFrameForDirectInvoke(
                        lastFrame,
                        currFrame,
                        callerRuntimeGraph,
                        nodeRef);
                    goto loop_start;
                }

                Frame *funcFrame = framePool_.acquire(runtimeTarget);
                camel::execute::fillFrameForDirectInvoke(
                    currFrame,
                    funcFrame,
                    callerRuntimeGraph,
                    nodeRef);
                slot_t callResult = call(runtimeTarget, funcFrame);

                currFrame->set(n->dataIndex, callResult);
            } break;

            case GCNodeKind::Oper: {
                auto *body =
                    const_cast<GCOperBody *>(currRuntimeGraph->nodeBodyAs<GCOperBody>(nodeRef));
                operator_t opFunc = body->op;
                if (!opFunc) {
                    const std::string uri(body->uri());
                    auto found = context_->execMgr().find(uri);
                    if (found) {
                        opFunc   = *found;
                        body->op = opFunc;
                    } else {
                        if (uri.starts_with(":mark/")) {
                            evalMarkedOperator(
                                uri.substr(6),
                                currRuntimeGraph,
                                nodeRef,
                                *currFrame);
                            break;
                        }
                        throwRuntimeFault(RuntimeDiag::UnrecognizedOperatorURI, uri);
                    }
                }

                operIndices_.clear();
                for (gc_node_ref_t inputRef : currRuntimeGraph->normInputsOf(nodeRef))
                    operIndices_.push_back(dataIndexOf(currRuntimeGraph, inputRef));
                size_t normCnt = operIndices_.size();
                for (gc_node_ref_t inputRef : currRuntimeGraph->withInputsOf(nodeRef))
                    operIndices_.push_back(dataIndexOf(currRuntimeGraph, inputRef));

                data_arr_t nargs{operIndices_.data(), normCnt};
                data_arr_t wargs{operIndices_.data() + normCnt, operIndices_.size() - normCnt};
                FrameArgsView withView(*currFrame, wargs);
                FrameArgsView normView(*currFrame, nargs);

                slot_t result = (*opFunc)(withView, normView, *context_);

                currFrame->set(n->dataIndex, result);
            } break;

            case GCNodeKind::Port:
                [[fallthrough]];
            case GCNodeKind::Data:
                [[fallthrough]];
            case GCNodeKind::Sync:
                [[fallthrough]];
            case GCNodeKind::Gate:
            case GCNodeKind::Dref:
                break;

            default: {
                ASSERT(
                    false,
                    std::format(
                        "Node type {} should not appear in NodeVM execution.",
                        static_cast<int>(n->kind)));
            } break;
            }

            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "NodeVM",
                "Executed node [{}/{}]: ref={}",
                i + 1,
                currNodes.size(),
                nodeRef));
        }
    }

        currRecursionDepth_--;

        result = camel::execute::readRuntimeGraphReturn(currRuntimeGraph, currFrame);

        // Release frames in the documented order from the header comment above.
        if (twinFrame != nullptr) {
            if (currFrame != rootFrame) {
                framePool_.release(currFrame);
            }
            if (twinFrame != rootFrame) {
                framePool_.release(twinFrame);
            }
        }
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
        auto sourceContext      = context_ ? context_->sourceContext() : nullptr;
        auto *faultRuntimeGraph = currFrame ? currFrame->runtimeGraph() : currRuntimeGraph;
        throw reportRuntimeFault(
            *context_,
            fault,
            faultRuntimeGraph
                ? makeGraphExecutionSite(sourceContext, faultRuntimeGraph, currRecursionDepth_)
                : makeGraphExecutionSite(
                      sourceContext,
                      static_cast<camel::runtime::GCGraph *>(nullptr),
                      currRecursionDepth_));
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

camel::runtime::GCGraph *NodeVMSchedPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    (void)os;
    ASSERT(graph != nullptr, "NodeVM requires a non-null runtime root graph.");
    graphCaches_.clear();

    Frame *rootFrame = framePool_.acquire(graph);
    slot_t result    = call(graph, rootFrame);
    context_->captureProcessExitCode(graph, result);
    return nullptr;
}

void NodeVMSchedPass::evalMarkedOperator(
    const std::string &uri, camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef,
    Frame &currFrame) {
    if (uri == "map_arr") {
        evalMarkedOperator_map_arr(graph, nodeRef, currFrame);
    } else if (uri == "apply_arr") {
        evalMarkedOperator_apply_arr(graph, nodeRef, currFrame);
    } else if (uri == "filter_arr") {
        evalMarkedOperator_filter_arr(graph, nodeRef, currFrame);
    } else if (uri == "reduce_arr" || uri == "unordered_reduce_arr") {
        evalMarkedOperator_reduce_arr(graph, nodeRef, currFrame);
    } else if (uri == "foreach_arr" || uri == "unordered_foreach_arr") {
        evalMarkedOperator_foreach_arr(graph, nodeRef, currFrame);
    } else {
        ASSERT(false, std::format("Mark operator {} not implemented in NodeVM.", uri));
    }
}

void NodeVMSchedPass::evalMarkedOperator_map_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "map_arr requires array and function inputs.");
    Array *arr     = currFrame.get<Array *>(dataIndexOf(graph, normInputs.front()));
    Function *func = currFrame.get<Function *>(dataIndexOf(graph, withInputs.front()));
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
    currFrame.set(dataIndexOf(graph, nodeRef), res);
}

void NodeVMSchedPass::evalMarkedOperator_apply_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "apply_arr requires array and function inputs.");
    Array *arr     = currFrame.get<Array *>(dataIndexOf(graph, normInputs.front()));
    Function *func = currFrame.get<Function *>(dataIndexOf(graph, withInputs.front()));
    Tuple *closure = func->tuple();
    slot_t *data   = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());
        frame->set(1, data[i]);
        for (size_t j = 0; j < closure->size(); ++j)
            frame->set(j + 2, closure->get<slot_t>(j));
        data[i] = call(func->graph(), frame);
    }
    currFrame.set(dataIndexOf(graph, nodeRef), arr);
}

void NodeVMSchedPass::evalMarkedOperator_filter_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "filter_arr requires array and function inputs.");
    Array *arr      = currFrame.get<Array *>(dataIndexOf(graph, normInputs.front()));
    Function *func  = currFrame.get<Function *>(dataIndexOf(graph, withInputs.front()));
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
    currFrame.set(dataIndexOf(graph, nodeRef), filtered);
}

void NodeVMSchedPass::evalMarkedOperator_reduce_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && withInputs.size() >= 2,
        "reduce_arr requires array, function, and initial value inputs.");
    Array *arr     = currFrame.get<Array *>(dataIndexOf(graph, normInputs.front()));
    Function *func = currFrame.get<Function *>(dataIndexOf(graph, withInputs[0]));
    slot_t init    = currFrame.get<slot_t>(dataIndexOf(graph, withInputs[1]));
    Tuple *closure = func->tuple();

    if (arr->size() == 0) {
        currFrame.set(dataIndexOf(graph, nodeRef), init);
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
    currFrame.set(dataIndexOf(graph, nodeRef), acc);
}

void NodeVMSchedPass::evalMarkedOperator_foreach_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "foreach_arr requires array and function inputs.");
    Array *arr     = currFrame.get<Array *>(dataIndexOf(graph, normInputs.front()));
    Function *func = currFrame.get<Function *>(dataIndexOf(graph, withInputs.front()));
    Tuple *closure = func->tuple();
    slot_t *from   = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());
        frame->set(1, from[i]);
        for (size_t j = 0; j < closure->size(); ++j)
            frame->set(j + 2, closure->get<slot_t>(j));
        call(func->graph(), frame);
    }
    currFrame.set(dataIndexOf(graph, nodeRef), NullSlot);
}
