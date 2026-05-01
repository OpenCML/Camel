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
 * Updated: May. 02, 2026
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

namespace {

constexpr size_t kNodeVmCacheSlot      = 0;
constexpr size_t kNodeVmCallLayoutSlot = 7;

using camel::runtime::gc_data_idx_t;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCAccsBody;
using camel::runtime::GCBrchBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::kInvalidNodeRef;

void bindMarkedFunctionFrame(
    Frame *frame, Function *func, std::span<const slot_t> normArgs,
    std::span<const slot_t> withArgs = {}) {
    ASSERT(frame != nullptr, "Marked operator call frame is null.");
    ASSERT(func != nullptr && func->graph() != nullptr, "Marked operator callee is null.");
    auto *graph             = func->graph();
    const auto normPorts    = graph->normPorts();
    const auto withPorts    = graph->withPorts();
    const auto closureNodes = graph->closureNodes();
    ASSERT(normArgs.size() == normPorts.size(), "Marked operator norm-arity mismatch.");
    ASSERT(withArgs.size() == withPorts.size(), "Marked operator with-arity mismatch.");

    Tuple *closure = func->tuple();
    ASSERT(closureNodes.empty() || closure != nullptr, "Marked operator closure tuple is null.");
    ASSERT(
        closure == nullptr || closure->size() == closureNodes.size(),
        "Marked operator closure-arity mismatch.");

    for (size_t i = 0; i < normPorts.size(); ++i) {
        const auto *port = graph->node(normPorts[i]);
        ASSERT(port != nullptr, "Marked operator norm port is missing.");
        frame->set(port->dataIndex, normArgs[i]);
    }
    for (size_t i = 0; i < withPorts.size(); ++i) {
        const auto *port = graph->node(withPorts[i]);
        ASSERT(port != nullptr, "Marked operator with port is missing.");
        frame->set(port->dataIndex, withArgs[i]);
    }
    if (closure != nullptr) {
        for (size_t i = 0; i < closureNodes.size(); ++i) {
            const auto *port = graph->node(closureNodes[i]);
            ASSERT(port != nullptr, "Marked operator closure node is missing.");
            frame->set(port->dataIndex, closure->get<slot_t>(i));
        }
    }
}

void validateVariableBodyBytes(const GCNode *node, size_t headerBytes, const char *what) {
    ASSERT(node != nullptr, "NodeVM variable-body validation received a null node.");
    ASSERT(
        node->bodyBytes() >= headerBytes,
        std::format("NodeVM {} payload is smaller than its fixed header.", what));
}

inline NodeVMGraphCache *nodeVmCacheOf(camel::runtime::GCGraph *graph) {
    return graph ? reinterpret_cast<NodeVMGraphCache *>(graph->extraSlot(kNodeVmCacheSlot))
                 : nullptr;
}

inline void setNodeVmCacheOf(camel::runtime::GCGraph *graph, NodeVMGraphCache *cache) {
    if (graph) {
        graph->setExtraSlot(kNodeVmCacheSlot, reinterpret_cast<uintptr_t>(cache));
    }
}

inline NodeVMCallLayoutCache *nodeVmCallLayoutOf(camel::runtime::GCGraph *graph) {
    return graph
               ? reinterpret_cast<NodeVMCallLayoutCache *>(graph->extraSlot(kNodeVmCallLayoutSlot))
               : nullptr;
}

inline void setNodeVmCallLayoutOf(camel::runtime::GCGraph *graph, NodeVMCallLayoutCache *cache) {
    if (graph) {
        graph->setExtraSlot(kNodeVmCallLayoutSlot, reinterpret_cast<uintptr_t>(cache));
    }
}

inline gc_data_idx_t dataIndexOf(const GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "NodeVM runtime node lookup resolved to null.");
    return node->dataIndex;
}

inline gc_data_idx_t dataIndexOf(const NodeVMGraphCache *cache, gc_node_ref_t nodeRef) {
    ASSERT(cache != nullptr, "NodeVM graph cache is null.");
    ASSERT(
        nodeRef < cache->dataIndexByRef.size(),
        "NodeVM cached data-index lookup is out of range.");
    return cache->dataIndexByRef[nodeRef];
}

inline std::span<const runtime_data_idx_t>
directCallArgSlotsOf(const NodeVMGraphCache *cache, size_t topoIndex) {
    ASSERT(cache != nullptr, "NodeVM direct-call cache is null.");
    ASSERT(
        topoIndex + 1 < cache->directCallArgOffsets.size(),
        "NodeVM direct-call arg offset lookup is out of range.");
    const uint32_t begin = cache->directCallArgOffsets[topoIndex];
    const uint32_t end   = cache->directCallArgOffsets[topoIndex + 1];
    ASSERT(
        begin <= end && end <= cache->directCallArgSlots.size(),
        "NodeVM direct-call cache is corrupt.");
    return std::span<const runtime_data_idx_t>(
        cache->directCallArgSlots.data() + begin,
        end - begin);
}

void bindDirectCallFrameSlots(
    Frame *sourceFrame, Frame *targetFrame, const NodeVMCallLayoutCache *layout,
    std::span<const runtime_data_idx_t> argSlots, std::vector<slot_t> &scratch) {
    ASSERT(sourceFrame != nullptr, "NodeVM direct-call source frame is null.");
    ASSERT(targetFrame != nullptr, "NodeVM direct-call target frame is null.");
    ASSERT(layout != nullptr, "NodeVM direct-call layout cache is null.");
    ASSERT(
        argSlots.size() == layout->calleePortSlots.size(),
        "NodeVM direct-call cache arity mismatch.");

    if (sourceFrame == targetFrame) {
        scratch.resize(argSlots.size());
        for (size_t argIndex = 0; argIndex < argSlots.size(); ++argIndex) {
            scratch[argIndex] = sourceFrame->get<slot_t>(argSlots[argIndex]);
        }
        for (size_t argIndex = 0; argIndex < argSlots.size(); ++argIndex) {
            targetFrame->set(layout->calleePortSlots[argIndex], scratch[argIndex]);
        }
        return;
    }

    for (size_t argIndex = 0; argIndex < argSlots.size(); ++argIndex) {
        targetFrame->set(
            layout->calleePortSlots[argIndex],
            sourceFrame->get<slot_t>(argSlots[argIndex]));
    }
}

} // namespace

NodeVMSchedPass::~NodeVMSchedPass() = default;

std::span<const gc_node_ref_t>
NodeVMSchedPass::buildTopoNodes(camel::runtime::GCGraph *runtimeGraph) {
    ASSERT(runtimeGraph != nullptr, "NodeVM runtime graph is null.");
    auto sortedNodeRefs       = camel::execute::buildReachableExecutionTopoIndices(runtimeGraph);
    const bool hasValueReturn = runtimeGraph->funcType() != nullptr &&
                                runtimeGraph->funcType()->hasExitType() &&
                                runtimeGraph->funcType()->exitType() != Type::Void();

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
    cache->directCallTailEligible.resize(cache->topoNodeRefs.size(), 0);
    cache->directCallFeedsTailJoin.resize(cache->topoNodeRefs.size(), 0);
    cache->directCallArgOffsets.resize(cache->topoNodeRefs.size() + 1, 0);
    cache->dataIndexByRef.resize(runtimeGraph->nodeBlockCount(), 0);
    cache->tailValueRef =
        hasValueReturn ? camel::execute::resolveRuntimeTailValueRef(runtimeGraph) : kInvalidNodeRef;
    const auto *tailValueNode =
        cache->tailValueRef != kInvalidNodeRef ? runtimeGraph->node(cache->tailValueRef) : nullptr;
    if (!tailValueNode || tailValueNode->dataIndex == 0) {
        cache->tailValueRef = kInvalidNodeRef;
        tailValueNode       = nullptr;
    }
    cache->tailValueIsJoin    = tailValueNode != nullptr && tailValueNode->kind == GCNodeKind::Join;
    cache->tailValueTopoIndex = cache->topoNodeRefs.size();

    for (size_t idx = 0; idx < cache->topoNodeRefs.size(); ++idx) {
        if (cache->topoNodeRefs[idx] == cache->tailValueRef) {
            cache->tailValueTopoIndex = idx;
            break;
        }
    }
    const bool anchorOk = cache->tailValueTopoIndex < cache->topoNodeRefs.size() &&
                          camel::execute::hasOnlyTrivialRuntimeTailSuffixAfter(
                              runtimeGraph,
                              cache->topoNodeRefs,
                              cache->tailValueTopoIndex);

    for (size_t idx = 0; idx < cache->topoNodeRefs.size(); ++idx) {
        const gc_node_ref_t nodeRef = cache->topoNodeRefs[idx];
        const auto *node            = runtimeGraph->node(nodeRef);
        if (node) {
            cache->dataIndexByRef[nodeRef] = node->dataIndex;
        }
        cache->directCallArgOffsets[idx] = static_cast<uint32_t>(cache->directCallArgSlots.size());
        if (!node || node->kind != GCNodeKind::Func) {
            continue;
        }
        cache->directCallTargets[idx] = runtimeGraph->directCalleeGraphOf(nodeRef);
        ASSERT(
            cache->directCallTargets[idx] != nullptr,
            "NodeVM direct FUNC target must be materialized into runtime metadata.");
        for (gc_node_ref_t argRef : runtimeGraph->normInputsOf(nodeRef)) {
            cache->directCallArgSlots.push_back(dataIndexOf(runtimeGraph, argRef));
        }
        for (gc_node_ref_t argRef : runtimeGraph->withInputsOf(nodeRef)) {
            cache->directCallArgSlots.push_back(dataIndexOf(runtimeGraph, argRef));
        }
        cache->directCallTailEligible[idx] = anchorOk && (nodeRef == cache->tailValueRef);
        if (anchorOk && cache->tailValueIsJoin) {
            cache->directCallFeedsTailJoin[idx] = camel::execute::runtimeNodeOutputsContain(
                runtimeGraph,
                nodeRef,
                cache->tailValueRef);
        }
    }
    cache->directCallArgOffsets[cache->topoNodeRefs.size()] =
        static_cast<uint32_t>(cache->directCallArgSlots.size());
    NodeVMGraphCache *cacheRaw = cache.get();
    graphCaches_.push_back(std::move(cache));
    setNodeVmCacheOf(runtimeGraph, cacheRaw);

    if (!nodeVmCallLayoutOf(runtimeGraph)) {
        auto layout          = std::make_unique<NodeVMCallLayoutCache>();
        const auto normPorts = runtimeGraph->normPorts();
        const auto withPorts = runtimeGraph->withPorts();
        layout->calleePortSlots.reserve(normPorts.size() + withPorts.size());
        for (gc_node_ref_t portRef : normPorts) {
            layout->calleePortSlots.push_back(dataIndexOf(runtimeGraph, portRef));
        }
        for (gc_node_ref_t portRef : withPorts) {
            layout->calleePortSlots.push_back(dataIndexOf(runtimeGraph, portRef));
        }
        NodeVMCallLayoutCache *layoutRaw = layout.get();
        callLayoutCaches_.push_back(std::move(layout));
        setNodeVmCallLayoutOf(runtimeGraph, layoutRaw);
    }
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
        auto *currCache                          = nodeVmCacheOf(currRuntimeGraph);
        ASSERT(currCache != nullptr, "NodeVM graph cache must be initialized before execution.");

        // Branch execution is implemented by skipping until the selected arm
        // head, executing through the arm tail, and then resuming at JOIN.
        gc_node_ref_t tillNode = kInvalidNodeRef;
        gc_node_ref_t skipNode = kInvalidNodeRef;
        gc_node_ref_t joinNode = kInvalidNodeRef;

        // Tail-call loop. Rebind currRuntimeGraph/currFrame instead of growing the C++ stack.
    loop_start: {
        const size_t nodesSize = currNodes.size();

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
                const auto srcIdx = dataIndexOf(currCache, normInputs.front());
                Type *srcType     = currFrame->typeAt<Type>(srcIdx);
                Type *tgtType     = n->dataType;
                slot_t value      = currFrame->get<slot_t>(srcIdx);
                slot_t result     = tgtType->castSlotFrom(value, srcType);
                currFrame->set(n->dataIndex, result);
            } break;

            case GCNodeKind::Copy: {
                const auto normInputs = currRuntimeGraph->normInputsOf(nodeRef);
                ASSERT(!normInputs.empty(), "COPY node must have one norm input.");
                gc_data_idx_t srcIdx = dataIndexOf(currCache, normInputs.front());
                TypeCode srcCode     = currFrame->codeAt(srcIdx);
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
                const auto srcIdx = dataIndexOf(currCache, normInputs.front());
                Type *srcType     = n->dataType;
                ASSERT(isGCTraced(srcType->code()), "FILL target type is not GC-traced in NodeVM.");
                Object *sourceObj = currFrame->get<Object *>(srcIdx);
                ASSERT(sourceObj != nullptr, "FILL source object is null in NodeVM.");
                Object *srcObj = sourceObj->clone(mm::autoSpace(), srcType, false);
                ASSERT(srcObj != nullptr, "FILL target data is null.");
                std::vector<slot_t> fillValues;
                fillValues.reserve(dataInputs.size());
                for (auto input : dataInputs) {
                    fillValues.push_back(currFrame->get<slot_t>(dataIndexOf(currCache, input)));
                }
                camel::execute::writeRuntimeFillSlots(
                    srcObj,
                    srcType,
                    currRuntimeGraph->nodeBodyAs<camel::runtime::GCFillBody>(nodeRef),
                    fillValues);
                currFrame->set(n->dataIndex, srcObj);
            } break;

            case GCNodeKind::Accs: {
                const auto normInputs = currRuntimeGraph->normInputsOf(nodeRef);
                ASSERT(!normInputs.empty(), "ACCS node must have one source input.");
                gc_data_idx_t srcIdx = dataIndexOf(currCache, normInputs.front());
                const auto *body     = currRuntimeGraph->nodeBodyAs<GCAccsBody>(nodeRef);
                validateVariableBodyBytes(n, sizeof(GCAccsBody), "ACCS");
                if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
                    size_t idx = body->value;
                    Tuple *t   = currFrame->get<Tuple *>(srcIdx);
                    ASSERT(idx < t->size(), "Tuple index out of bounds in NodeVM.");
                    currFrame->set(n->dataIndex, t->get<slot_t>(idx));
                } else {
                    ASSERT(
                        body->keyBytes <= n->bodyBytes() - sizeof(GCAccsBody),
                        "NodeVM ACCS struct-key payload exceeds the node body.");
                    const std::string_view keyView = body->key();
                    std::string key                = std::string(keyView.data(), keyView.size());
                    Struct *s                      = currFrame->get<Struct *>(srcIdx);
                    Type *structType               = currFrame->typeAt<Type>(srcIdx);
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
                int32_t brIndex = currFrame->get<int32_t>(dataIndexOf(currCache, nargs.front()));
                ASSERT(
                    brIndex >= 0 && static_cast<size_t>(brIndex) < wargs.size(),
                    "JOIN branch index out of range in NodeVM.");
                if (n->dataIndex == 0) {
                    break;
                }
                if (n->dataType == Type::Void()) {
                    currFrame->set(n->dataIndex, NullSlot);
                    break;
                }
                // The selected arm has already executed sequentially. Its tail
                // slot now holds the branch result consumed by JOIN.
                slot_t branchResult = currFrame->get<slot_t>(
                    dataIndexOf(currCache, wargs[static_cast<size_t>(brIndex)]));
                currFrame->set(n->dataIndex, branchResult);
            } break;

            case GCNodeKind::Call: {
                const auto withInputs = currRuntimeGraph->withInputsOf(nodeRef);
                ASSERT(!withInputs.empty(), "CALL node must have exactly one callee input");
                Function *func =
                    currFrame->get<Function *>(dataIndexOf(currCache, withInputs.front()));
                ASSERT(func != nullptr, "NodeVM CALL resolved a null Function callee.");
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
                if (n->dataIndex != 0) {
                    currFrame->set(n->dataIndex, callResult);
                }
            } break;

            case GCNodeKind::Func: {
                auto *callerRuntimeGraph = currRuntimeGraph;
                auto *runtimeTarget      = currCache->directCallTargets[i];
                ASSERT(
                    runtimeTarget != nullptr,
                    "NodeVM direct FUNC target must have a materialized runtime graph.");
                const bool isTailCall = currCache->directCallTailEligible[i] != 0 ||
                                        currCache->directCallFeedsTailJoin[i] != 0;
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
                        currCache        = nodeVmCacheOf(currRuntimeGraph);
                        ASSERT(
                            currCache != nullptr,
                            "NodeVM callee cache must be initialized before tail-call execution.");

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
                            const auto argSlots =
                                directCallArgSlotsOf(nodeVmCacheOf(callerRuntimeGraph), i);
                            topoNodesFor(runtimeTarget);
                            auto *layout = nodeVmCallLayoutOf(runtimeTarget);
                            ASSERT(layout != nullptr, "NodeVM call layout cache must exist.");
                            bindDirectCallFrameSlots(
                                lastFrame,
                                funcFrame,
                                layout,
                                argSlots,
                                callArgScratch_);

                            currFrame = funcFrame;
                            goto loop_start;
                        }
                    }

                    // Self recursion and mutual recursion both land here to
                    // refresh the callee-visible argument slots.
                    const auto argSlots =
                        directCallArgSlotsOf(nodeVmCacheOf(callerRuntimeGraph), i);
                    topoNodesFor(runtimeTarget);
                    auto *layout = nodeVmCallLayoutOf(runtimeTarget);
                    ASSERT(layout != nullptr, "NodeVM call layout cache must exist.");
                    bindDirectCallFrameSlots(
                        lastFrame,
                        currFrame,
                        layout,
                        argSlots,
                        callArgScratch_);
                    goto loop_start;
                }

                Frame *funcFrame    = framePool_.acquire(runtimeTarget);
                const auto argSlots = directCallArgSlotsOf(currCache, i);
                topoNodesFor(runtimeTarget);
                auto *layout = nodeVmCallLayoutOf(runtimeTarget);
                ASSERT(layout != nullptr, "NodeVM call layout cache must exist.");
                bindDirectCallFrameSlots(currFrame, funcFrame, layout, argSlots, callArgScratch_);
                slot_t callResult = call(runtimeTarget, funcFrame);

                if (n->dataIndex != 0) {
                    currFrame->set(n->dataIndex, callResult);
                }
            } break;

            case GCNodeKind::Oper: {
                auto *body =
                    const_cast<GCOperBody *>(currRuntimeGraph->nodeBodyAs<GCOperBody>(nodeRef));
                validateVariableBodyBytes(n, sizeof(GCOperBody), "OPER");
                ASSERT(
                    body->uriBytes <= n->bodyBytes() - sizeof(GCOperBody),
                    "NodeVM OPER uri payload exceeds the node body.");
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
                    operIndices_.push_back(dataIndexOf(currCache, inputRef));
                size_t normCnt = operIndices_.size();
                for (gc_node_ref_t inputRef : currRuntimeGraph->withInputsOf(nodeRef))
                    operIndices_.push_back(dataIndexOf(currCache, inputRef));

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
        if (currFrame != nullptr && currFrame != rootFrame) {
            framePool_.release(currFrame);
        }
        if (twinFrame != nullptr && twinFrame != rootFrame && twinFrame != currFrame) {
            framePool_.release(twinFrame);
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
    const auto arrSlot  = dataIndexOf(graph, normInputs.front());
    const auto funcSlot = dataIndexOf(graph, withInputs.front());
    Array *arr          = currFrame.get<Array *>(arrSlot);
    Array *res          = Array::create(mm::autoSpace(), arr->size());
    currFrame.set(dataIndexOf(graph, nodeRef), res);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr            = currFrame.get<Array *>(arrSlot);
        Function *func = currFrame.get<Function *>(funcSlot);
        slot_t element = arr->data()[i];
        Frame *frame   = framePool_.acquire(func->graph());
        bindMarkedFunctionFrame(frame, func, std::span<const slot_t>(&element, 1));
        Array *target     = currFrame.get<Array *>(dataIndexOf(graph, nodeRef));
        target->data()[i] = call(func->graph(), frame);
    }
}

void NodeVMSchedPass::evalMarkedOperator_apply_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "apply_arr requires array and function inputs.");
    const auto arrSlot  = dataIndexOf(graph, normInputs.front());
    const auto funcSlot = dataIndexOf(graph, withInputs.front());
    Array *arr          = currFrame.get<Array *>(arrSlot);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr            = currFrame.get<Array *>(arrSlot);
        Function *func = currFrame.get<Function *>(funcSlot);
        slot_t element = arr->data()[i];
        Frame *frame   = framePool_.acquire(func->graph());
        bindMarkedFunctionFrame(frame, func, std::span<const slot_t>(&element, 1));
        arr->data()[i] = call(func->graph(), frame);
    }
    currFrame.set(dataIndexOf(graph, nodeRef), currFrame.get<Array *>(arrSlot));
}

void NodeVMSchedPass::evalMarkedOperator_filter_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "filter_arr requires array and function inputs.");
    const auto arrSlot  = dataIndexOf(graph, normInputs.front());
    const auto funcSlot = dataIndexOf(graph, withInputs.front());
    Array *arr          = currFrame.get<Array *>(arrSlot);
    Array *filtered     = Array::create(mm::autoSpace(), arr->size());
    currFrame.set(dataIndexOf(graph, nodeRef), filtered);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr            = currFrame.get<Array *>(arrSlot);
        Function *func = currFrame.get<Function *>(funcSlot);
        slot_t element = arr->data()[i];
        Frame *frame   = framePool_.acquire(func->graph());
        bindMarkedFunctionFrame(frame, func, std::span<const slot_t>(&element, 1));
        slot_t result = call(func->graph(), frame);
        if (fromSlot<bool>(result)) {
            Array *target = currFrame.get<Array *>(dataIndexOf(graph, nodeRef));
            target->append(arr->data()[i]);
        }
    }
    currFrame.get<Array *>(dataIndexOf(graph, nodeRef))->shrinkToFit();
}

void NodeVMSchedPass::evalMarkedOperator_reduce_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && withInputs.size() >= 2,
        "reduce_arr requires array, function, and initial value inputs.");
    const auto arrSlot    = dataIndexOf(graph, normInputs.front());
    const auto funcSlot   = dataIndexOf(graph, withInputs[0]);
    const auto initSlot   = dataIndexOf(graph, withInputs[1]);
    const auto resultSlot = dataIndexOf(graph, nodeRef);
    Array *arr            = currFrame.get<Array *>(arrSlot);
    slot_t init           = currFrame.get<slot_t>(initSlot);

    if (arr->size() == 0) {
        currFrame.set(resultSlot, init);
        return;
    }
    currFrame.set(resultSlot, init);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr                 = currFrame.get<Array *>(arrSlot);
        Function *func      = currFrame.get<Function *>(funcSlot);
        const slot_t args[] = {currFrame.get<slot_t>(resultSlot), arr->data()[i]};
        Frame *frame        = framePool_.acquire(func->graph());
        bindMarkedFunctionFrame(frame, func, std::span<const slot_t>(args, 2));
        currFrame.set(resultSlot, call(func->graph(), frame));
    }
}

void NodeVMSchedPass::evalMarkedOperator_foreach_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "foreach_arr requires array and function inputs.");
    const auto arrSlot  = dataIndexOf(graph, normInputs.front());
    const auto funcSlot = dataIndexOf(graph, withInputs.front());
    Array *arr          = currFrame.get<Array *>(arrSlot);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr            = currFrame.get<Array *>(arrSlot);
        Function *func = currFrame.get<Function *>(funcSlot);
        slot_t element = arr->data()[i];
        Frame *frame   = framePool_.acquire(func->graph());
        bindMarkedFunctionFrame(frame, func, std::span<const slot_t>(&element, 1));
        call(func->graph(), frame);
    }
    currFrame.set(dataIndexOf(graph, nodeRef), NullSlot);
}
