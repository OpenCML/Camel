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
 * Created: Oct. 05, 2025
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "taskflow.h"

#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/execute/executor.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>

using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

using runtime_data_idx_t = camel::compile::gir::data_idx_t;

using camel::execute::RuntimeBranchArmRegion;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCAccsBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::kInvalidNodeRef;

struct TaskflowHigherOrderCallSite {
    GCGraph *runtimeGraph = nullptr;
    Tuple *closure        = nullptr;
};

inline runtime_data_idx_t dataIndexOf(const GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "Taskflow runtime node lookup resolved to null.");
    return node->dataIndex;
}

inline const GCNode *requireNode(const GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "Taskflow runtime node lookup resolved to null.");
    return node;
}

slot_t readForwardedRuntimeValue(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow forwarded-value read requires a graph.");
    ASSERT(frame != nullptr, "Taskflow forwarded-value read requires a frame.");

    const gc_node_ref_t current = camel::execute::resolveRuntimeForwardedValueRef(graph, nodeRef);
    if (current == kInvalidNodeRef) {
        return NullSlot;
    }
    const auto *node = requireNode(graph, current);
    ASSERT(node->dataIndex != 0, "Taskflow forwarded-value chain resolved to slot 0.");
    return frame->get<slot_t>(node->dataIndex);
}

TaskflowHigherOrderCallSite makeHigherOrderCallSite(Function *func) {
    auto *runtimeGraph = func ? func->graph() : nullptr;
    ASSERT(runtimeGraph != nullptr, "Taskflow higher-order call requires a runtime graph target.");
    return {
        .runtimeGraph = runtimeGraph,
        .closure      = func ? func->tuple() : nullptr,
    };
}

} // namespace

TaskflowExecSchedPass::RuntimeBuildInfo
TaskflowExecSchedPass::buildRuntimeBuildInfo(GCGraph *graph) {
    ASSERT(graph != nullptr, "Taskflow build info requires a runtime graph.");

    RuntimeBuildInfo info;
    info.skipNodes.resize(graph->nodeBlockCount(), 0);
    info.branchArms.resize(graph->nodeBlockCount());

    for (gc_node_ref_t nodeRef : camel::execute::buildReachableExecutionTopoIndices(graph)) {
        const auto *node = requireNode(graph, nodeRef);
        if (node->kind != GCNodeKind::Brch) {
            continue;
        }
        const auto ctrlOutputs = graph->ctrlOutputsOf(nodeRef);
        auto &arms             = info.branchArms[nodeRef];
        arms.reserve(ctrlOutputs.size());
        for (size_t armIndex = 0; armIndex < ctrlOutputs.size(); ++armIndex) {
            auto arm = camel::execute::collectRuntimeBranchArmRegion(graph, nodeRef, armIndex);
            for (gc_node_ref_t bodyRef : arm.topoIndices) {
                if (bodyRef < info.skipNodes.size()) {
                    info.skipNodes[bodyRef] = 1;
                }
            }
            if (arm.joinIndex < info.skipNodes.size()) {
                info.skipNodes[arm.joinIndex] = 1;
            }
            arms.push_back(std::move(arm));
        }
    }

    framePool_.warmup(graph, 1);
    return info;
}

camel::runtime::GCGraph *
TaskflowExecSchedPass::apply(camel::runtime::GCGraph *graph, std::ostream & /*os*/) {
    ASSERT(graph != nullptr, "Taskflow requires a non-null runtime root graph.");

    linearTopoCache_.clear();
    Frame *rootFrame = framePool_.acquire(graph);
    try {
        slot_t result = evalGraphTF(graph, rootFrame);
        context_->captureProcessExitCode(graph, result);
        framePool_.release(rootFrame);
    } catch (...) {
        framePool_.release(rootFrame);
        throw;
    }
    return nullptr;
}

slot_t TaskflowExecSchedPass::evalGraphTF(GCGraph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow evaluation requires a runtime graph.");
    return evalGraphLinear(graph, frame);
}

std::span<const gc_node_ref_t> TaskflowExecSchedPass::topoNodesForLinear(GCGraph *graph) {
    ASSERT(graph != nullptr, "Taskflow linear topo lookup requires a runtime graph.");
    auto [it, inserted] = linearTopoCache_.try_emplace(graph);
    if (inserted) {
        it->second = camel::execute::buildReachableExecutionTopoIndices(graph);
    }
    return std::span<const gc_node_ref_t>(it->second);
}

slot_t TaskflowExecSchedPass::evalGraphLinear(GCGraph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow linear evaluation requires a runtime graph.");
    Frame *rootFrame       = frame;
    Frame *currFrame       = frame;
    Frame *twinFrame       = nullptr;
    auto *currRuntimeGraph = graph;
    auto currNodes         = topoNodesForLinear(currRuntimeGraph);

    gc_node_ref_t tillNode = kInvalidNodeRef;
    gc_node_ref_t skipNode = kInvalidNodeRef;
    gc_node_ref_t joinNode = kInvalidNodeRef;

loop_start:
    const gc_node_ref_t lastNode = camel::execute::resolveRuntimeTailValueRef(currRuntimeGraph);
    const bool lastNodeIsJoin =
        lastNode != kInvalidNodeRef && currRuntimeGraph->node(lastNode)->kind == GCNodeKind::Join;

    for (size_t i = 0; i < currNodes.size(); ++i) {
        const gc_node_ref_t nodeRef = currNodes[i];
        const auto *node            = requireNode(currRuntimeGraph, nodeRef);

        if (tillNode != kInvalidNodeRef) {
            if (tillNode == nodeRef) {
                tillNode = kInvalidNodeRef;
            } else {
                continue;
            }
        }
        if (skipNode != kInvalidNodeRef && skipNode == nodeRef) {
            skipNode = kInvalidNodeRef;
            tillNode = joinNode;
        }

        if (node->kind == GCNodeKind::Brch) {
            const size_t jumpIdx =
                camel::execute::selectRuntimeBranchArm(currRuntimeGraph, nodeRef, currFrame);
            currFrame->set(node->dataIndex, static_cast<Int32>(jumpIdx));

            const auto arms  = currRuntimeGraph->branchArmsOf(nodeRef);
            const auto *body = currRuntimeGraph->nodeBodyAs<camel::runtime::GCBrchBody>(nodeRef);
            ASSERT(jumpIdx < arms.size(), "Taskflow linear BRCH arm index is out of range.");
            tillNode = arms[jumpIdx].head;
            skipNode = arms[jumpIdx].tail;
            joinNode = body->join;
            continue;
        }

        if (node->kind == GCNodeKind::Func) {
            auto *callerRuntimeGraph = currRuntimeGraph;
            auto *runtimeTarget      = currRuntimeGraph->directCalleeGraphOf(nodeRef);
            ASSERT(runtimeTarget != nullptr, "Taskflow linear FUNC target graph is null.");

            size_t anchorIdx = currNodes.size();
            for (size_t k = 0; k < currNodes.size(); ++k) {
                if (currNodes[k] == lastNode) {
                    anchorIdx = k;
                    break;
                }
            }
            const bool anchorOk = anchorIdx < currNodes.size() &&
                                  camel::execute::hasOnlyTrivialRuntimeTailSuffixAfter(
                                      currRuntimeGraph,
                                      currNodes,
                                      anchorIdx);
            const bool tailShape =
                (nodeRef == lastNode) ||
                (lastNodeIsJoin &&
                 camel::execute::runtimeNodeOutputsContain(currRuntimeGraph, nodeRef, lastNode));
            const bool isTailCall = anchorOk && tailShape;
            if (isTailCall) {
                Frame *lastFrame = currFrame;
                tillNode         = kInvalidNodeRef;
                skipNode         = kInvalidNodeRef;
                joinNode         = kInvalidNodeRef;

                if (runtimeTarget == currRuntimeGraph) {
                    camel::execute::fillFrameForDirectInvoke(
                        lastFrame,
                        currFrame,
                        callerRuntimeGraph,
                        nodeRef);
                    goto loop_start;
                }

                currRuntimeGraph = runtimeTarget;
                currNodes        = topoNodesForLinear(currRuntimeGraph);

                if (twinFrame && twinFrame->runtimeGraph() == runtimeTarget) {
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

                camel::execute::fillFrameForDirectInvoke(
                    lastFrame,
                    currFrame,
                    callerRuntimeGraph,
                    nodeRef);
                goto loop_start;
            }
        }

        (void)executeLinearNode(currRuntimeGraph, nodeRef, currFrame);
    }

    slot_t result = camel::execute::readRuntimeGraphReturn(currRuntimeGraph, currFrame);
    if (twinFrame != nullptr) {
        if (currFrame != rootFrame) {
            framePool_.release(currFrame);
        }
        if (twinFrame != rootFrame) {
            framePool_.release(twinFrame);
        }
    }
    return result;
}

slot_t TaskflowExecSchedPass::runPreparedSubgraph(tf::Subflow &sf, GCGraph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow subgraph execution requires a runtime graph.");
    (void)sf;
    try {
        slot_t result = evalGraphLinear(graph, frame);
        framePool_.release(frame);
        return result;
    } catch (...) {
        framePool_.release(frame);
        throw;
    }
}

slot_t TaskflowExecSchedPass::executeLinearMarkedOperator(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    tf::Taskflow flow;
    flow.emplace([this, graph, nodeRef, frame](tf::Subflow &sf) {
        const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
        const std::string uri(body->uri());
        if (uri == ":mark/map_arr") {
            mark_map_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/apply_arr") {
            mark_apply_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/filter_arr") {
            mark_filter_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/reduce_arr") {
            mark_reduce_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/unordered_reduce_arr") {
            mark_unordered_reduce_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/foreach_arr") {
            mark_foreach_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/unordered_foreach_arr") {
            mark_unordered_foreach_arr(graph, nodeRef, frame, sf);
        }
    });
    tf::Executor nestedExecutor;
    nestedExecutor.run(flow).wait();
    const auto *node = requireNode(graph, nodeRef);
    return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);
}

slot_t
TaskflowExecSchedPass::executeLinearNode(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    const auto *node = requireNode(graph, nodeRef);

    switch (node->kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Sync:
        return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);

    case GCNodeKind::Join: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear JOIN must have branch-index input.");
        const auto brIndex = frame->get<Int32>(dataIndexOf(graph, normInputs.front()));
        ASSERT(
            brIndex >= 0 && static_cast<size_t>(brIndex) < withInputs.size(),
            "Taskflow linear JOIN branch index is out of range.");
        const slot_t result =
            frame->get<slot_t>(dataIndexOf(graph, withInputs[static_cast<size_t>(brIndex)]));
        if (node->dataIndex != 0) {
            frame->set(node->dataIndex, result);
        }
        return result;
    }

    case GCNodeKind::Gate: {
        const slot_t value = readForwardedRuntimeValue(graph, nodeRef, frame);
        if (node->dataIndex != 0) {
            frame->set(node->dataIndex, value);
        }
        return value;
    }

    case GCNodeKind::Copy: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear COPY node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const TypeCode srcCode          = frame->codeAt(srcIdx);
        if (camel::core::type::isGCTraced(srcCode)) {
            Object *srcData  = frame->get<Object *>(srcIdx);
            Type *srcTypePtr = frame->typeAt<Type>(srcIdx);
            auto *copy       = srcData->clone(camel::core::mm::autoSpace(), srcTypePtr, false);
            frame->set(node->dataIndex, copy);
            return toSlot(copy);
        }
        const slot_t value = frame->get<slot_t>(srcIdx);
        frame->set(node->dataIndex, value);
        return value;
    }

    case GCNodeKind::Cast: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear CAST node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        Type *srcType                   = frame->typeAt<Type>(srcIdx);
        Type *tgtType                   = node->dataType;
        slot_t value                    = frame->get<slot_t>(srcIdx);
        slot_t result                   = tgtType->castSlotFrom(value, srcType);
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Fill: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear FILL node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const TypeCode srcCode          = frame->codeAt(srcIdx);
        Type *srcType                   = frame->typeAt<Type>(srcIdx);
        ASSERT(
            camel::core::type::isGCTraced(srcCode),
            "Taskflow linear FILL target must be GC-traced.");
        Object *srcObj =
            frame->get<Object *>(srcIdx)->clone(camel::core::mm::autoSpace(), srcType, false);
        ASSERT(srcObj != nullptr, "Taskflow linear FILL source object is null.");

        switch (srcCode) {
        case TypeCode::Tuple: {
            auto *type         = tt::as_ptr<TupleType>(srcType);
            auto *tuple        = tt::as_ptr<Tuple>(srcObj);
            const size_t *refs = type->refs();
            for (size_t i = 0; i < withInputs.size(); ++i) {
                tuple->set<slot_t>(refs[i], frame->get<slot_t>(dataIndexOf(graph, withInputs[i])));
            }
        } break;
        case TypeCode::Array: {
            auto *array = tt::as_ptr<Array>(srcObj);
            for (size_t i = 0; i < withInputs.size(); ++i) {
                array->set<slot_t>(i, frame->get<slot_t>(dataIndexOf(graph, withInputs[i])));
            }
        } break;
        case TypeCode::Struct: {
            auto *type         = tt::as_ptr<StructType>(srcType);
            auto *st           = tt::as_ptr<Struct>(srcObj);
            const size_t *refs = type->refs();
            for (size_t i = 0; i < withInputs.size(); ++i) {
                st->set<slot_t>(refs[i], frame->get<slot_t>(dataIndexOf(graph, withInputs[i])));
            }
        } break;
        case TypeCode::Function: {
            auto *func         = tt::as_ptr<Function>(srcObj);
            Tuple *closureData = func->tuple();
            ASSERT(closureData != nullptr, "Taskflow linear FILL function closure is null.");
            for (size_t i = 0; i < withInputs.size(); ++i) {
                closureData->set<slot_t>(i, frame->get<slot_t>(dataIndexOf(graph, withInputs[i])));
            }
        } break;
        default:
            ASSERT(
                false,
                std::format(
                    "Unsupported Taskflow linear FILL type {}.",
                    typeCodeToString(srcCode)));
        }

        frame->set(node->dataIndex, srcObj);
        return toSlot(srcObj);
    }

    case GCNodeKind::Accs: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear ACCS node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const auto *body                = graph->nodeBodyAs<GCAccsBody>(nodeRef);
        if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
            Tuple *tuple = frame->get<Tuple *>(srcIdx);
            ASSERT(body->value < tuple->size(), "Taskflow linear ACCS tuple index out of bounds.");
            const slot_t value = tuple->get<slot_t>(body->value);
            frame->set(node->dataIndex, value);
            return value;
        }
        Struct *st   = frame->get<Struct *>(srcIdx);
        Type *stType = frame->typeAt<Type>(srcIdx);
        slot_t value = st->get<slot_t>(std::string(body->key()), stType);
        frame->set(node->dataIndex, value);
        return value;
    }

    case GCNodeKind::Func: {
        auto *targetGraph = graph->directCalleeGraphOf(nodeRef);
        ASSERT(targetGraph != nullptr, "Taskflow linear FUNC target graph is null.");
        Frame *calleeFrame = acquirePreparedRuntimeCallFrame(targetGraph, nodeRef, frame);
        slot_t result      = NullSlot;
        try {
            result = evalGraphLinear(targetGraph, calleeFrame);
            framePool_.release(calleeFrame);
        } catch (...) {
            framePool_.release(calleeFrame);
            throw;
        }
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Call: {
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!withInputs.empty(), "Taskflow linear CALL node must have function input.");
        Function *func    = frame->get<Function *>(dataIndexOf(graph, withInputs.front()));
        auto *targetGraph = func ? func->graph() : nullptr;
        ASSERT(targetGraph != nullptr, "Taskflow linear CALL target graph is null.");
        Frame *calleeFrame = acquirePreparedRuntimeCallFrame(targetGraph, nodeRef, frame);
        slot_t result      = NullSlot;
        try {
            result = evalGraphLinear(targetGraph, calleeFrame);
            framePool_.release(calleeFrame);
        } catch (...) {
            framePool_.release(calleeFrame);
            throw;
        }
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Oper: {
        const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
        const std::string uri(body->uri());
        if (uri.starts_with(":mark/")) {
            return executeLinearMarkedOperator(graph, nodeRef, frame);
        }
        const slot_t result = executePreparedOperator(graph, nodeRef, frame);
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Brch:
        return NullSlot;

    case GCNodeKind::Bind:
    case GCNodeKind::Dref:
        return NullSlot;

    default: {
        tf::Taskflow flow;
        slot_t result = NullSlot;
        flow.emplace([this, graph, nodeRef, frame, &result](tf::Subflow &sf) {
            result = executePreparedNode(graph, nodeRef, frame, sf, nullptr);
        });
        tf::Executor nestedExecutor;
        nestedExecutor.run(flow).wait();
        return result;
    }
    }
}

Frame *TaskflowExecSchedPass::acquirePreparedRuntimeCallFrame(
    GCGraph *targetGraph, gc_node_ref_t callNodeRuntimeIndex, Frame *sourceFrame) {
    ASSERT(targetGraph != nullptr, "Taskflow runtime call target graph is null.");
    ASSERT(sourceFrame != nullptr, "Taskflow runtime source frame is null.");
    ASSERT(sourceFrame->runtimeGraph() != nullptr, "Taskflow runtime source graph is null.");

    Frame *dest            = framePool_.acquire(targetGraph);
    const auto *callRecord = requireNode(sourceFrame->runtimeGraph(), callNodeRuntimeIndex);
    if (callRecord->kind == GCNodeKind::Call) {
        camel::execute::fillFrameForIndirectCall(
            sourceFrame,
            dest,
            sourceFrame->runtimeGraph(),
            callNodeRuntimeIndex);
    } else {
        camel::execute::fillFrameForDirectInvoke(
            sourceFrame,
            dest,
            sourceFrame->runtimeGraph(),
            callNodeRuntimeIndex);
    }
    return dest;
}

Frame *TaskflowExecSchedPass::acquirePreparedClosureCallFrame(
    GCGraph *targetGraph, Tuple *closure, std::span<const slot_t> args) {
    ASSERT(targetGraph != nullptr, "Taskflow closure call requires a runtime graph target.");
    Frame *dest = framePool_.acquire(targetGraph);

    const auto normPorts = targetGraph->normPorts();
    ASSERT(args.size() == normPorts.size(), "Taskflow closure call norm-arg count mismatch.");
    for (size_t i = 0; i < args.size(); ++i) {
        dest->set(dataIndexOf(targetGraph, normPorts[i]), args[i]);
    }

    const auto closureNodes = targetGraph->closureNodes();
    if (closureNodes.empty()) {
        return dest;
    }

    ASSERT(closure != nullptr, "Taskflow closure tuple is null.");
    ASSERT(closure->size() == closureNodes.size(), "Taskflow closure size mismatch.");
    for (size_t i = 0; i < closureNodes.size(); ++i) {
        dest->set(dataIndexOf(targetGraph, closureNodes[i]), closure->get<slot_t>(i));
    }
    return dest;
}

slot_t TaskflowExecSchedPass::executePreparedOperator(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow operator execution requires a graph.");
    ASSERT(frame != nullptr, "Taskflow operator execution requires a frame.");

    const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
    const std::string uri(body->uri());
    auto opFunc = context_->execMgr().find(uri);
    if (!opFunc) {
        throw reportRuntimeFault(
            *context_,
            RuntimeFault::make(RuntimeDiag::UnrecognizedOperatorURI, uri),
            makeGraphExecutionSite(context_->sourceContext(), graph, 0, "taskflow"));
    }

    std::vector<runtime_data_idx_t> withIndices;
    std::vector<runtime_data_idx_t> normIndices;
    withIndices.reserve(graph->withInputsOf(nodeRef).size());
    normIndices.reserve(graph->normInputsOf(nodeRef).size());
    for (gc_node_ref_t inputRef : graph->withInputsOf(nodeRef)) {
        withIndices.push_back(dataIndexOf(graph, inputRef));
    }
    for (gc_node_ref_t inputRef : graph->normInputsOf(nodeRef)) {
        normIndices.push_back(dataIndexOf(graph, inputRef));
    }

    data_arr_t wargs{withIndices.data(), withIndices.size()};
    data_arr_t nargs{normIndices.data(), normIndices.size()};
    FrameArgsView withView(*frame, wargs);
    FrameArgsView normView(*frame, nargs);

    try {
        return (*opFunc)(withView, normView, *context_);
    } catch (const RuntimeFault &fault) {
        throw reportRuntimeFault(
            *context_,
            fault,
            makeGraphExecutionSite(context_->sourceContext(), graph, 0, "taskflow"));
    }
}

slot_t TaskflowExecSchedPass::executePreparedBranchArm(
    GCGraph *graph, gc_node_ref_t brchRef, size_t armIndex, Frame *frame, tf::Subflow &sf,
    const RuntimeBuildInfo *buildInfo) {
    ASSERT(graph != nullptr, "Taskflow branch execution requires a graph.");
    RuntimeBranchArmRegion ownedArm;
    const RuntimeBranchArmRegion *arm = nullptr;
    if (buildInfo != nullptr && brchRef < buildInfo->branchArms.size()) {
        const auto &arms = buildInfo->branchArms[brchRef];
        ASSERT(armIndex < arms.size(), "Taskflow branch arm index is out of range.");
        arm = &arms[armIndex];
    } else {
        ownedArm = camel::execute::collectRuntimeBranchArmRegion(graph, brchRef, armIndex);
        arm      = &ownedArm;
    }
    ASSERT(arm != nullptr, "Taskflow branch arm resolution failed.");
    for (gc_node_ref_t nodeRef : arm->topoIndices) {
        (void)executePreparedNode(graph, nodeRef, frame, sf, buildInfo);
    }

    ASSERT(arm->tailIndex != kInvalidNodeRef, "Taskflow branch arm has no tail.");
    return frame->get<slot_t>(dataIndexOf(graph, arm->tailIndex));
}

slot_t TaskflowExecSchedPass::executePreparedNode(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf,
    const RuntimeBuildInfo *buildInfo) {
    const auto *node = requireNode(graph, nodeRef);

    switch (node->kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Sync:
        return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);

    case GCNodeKind::Join: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow JOIN must have branch-index input.");
        const auto brIndex = frame->get<Int32>(dataIndexOf(graph, normInputs.front()));
        ASSERT(
            brIndex >= 0 && static_cast<size_t>(brIndex) < withInputs.size(),
            "Taskflow JOIN branch index is out of range.");
        const slot_t result =
            frame->get<slot_t>(dataIndexOf(graph, withInputs[static_cast<size_t>(brIndex)]));
        if (node->dataIndex != 0) {
            frame->set(node->dataIndex, result);
        }
        return result;
    }

    case GCNodeKind::Gate: {
        const slot_t value = readForwardedRuntimeValue(graph, nodeRef, frame);
        if (node->dataIndex != 0) {
            frame->set(node->dataIndex, value);
        }
        return value;
    }

    case GCNodeKind::Copy: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow COPY node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const TypeCode srcCode          = frame->codeAt(srcIdx);
        if (camel::core::type::isGCTraced(srcCode)) {
            Object *srcData  = frame->get<Object *>(srcIdx);
            Type *srcTypePtr = frame->typeAt<Type>(srcIdx);
            auto *copy       = srcData->clone(camel::core::mm::autoSpace(), srcTypePtr, false);
            frame->set(node->dataIndex, copy);
            return toSlot(copy);
        }
        const slot_t value = frame->get<slot_t>(srcIdx);
        frame->set(node->dataIndex, value);
        return value;
    }

    case GCNodeKind::Cast: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow CAST node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        Type *srcType                   = frame->typeAt<Type>(srcIdx);
        Type *tgtType                   = node->dataType;
        slot_t value                    = frame->get<slot_t>(srcIdx);
        slot_t result                   = tgtType->castSlotFrom(value, srcType);
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Fill: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow FILL node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const TypeCode srcCode          = frame->codeAt(srcIdx);
        Type *srcType                   = frame->typeAt<Type>(srcIdx);
        ASSERT(camel::core::type::isGCTraced(srcCode), "Taskflow FILL target must be GC-traced.");
        Object *srcObj =
            frame->get<Object *>(srcIdx)->clone(camel::core::mm::autoSpace(), srcType, false);
        ASSERT(srcObj != nullptr, "Taskflow FILL source object is null.");

        switch (srcCode) {
        case TypeCode::Tuple: {
            auto *type         = tt::as_ptr<TupleType>(srcType);
            auto *tuple        = tt::as_ptr<Tuple>(srcObj);
            const size_t *refs = type->refs();
            for (size_t i = 0; i < withInputs.size(); ++i) {
                tuple->set<slot_t>(refs[i], frame->get<slot_t>(dataIndexOf(graph, withInputs[i])));
            }
        } break;
        case TypeCode::Array: {
            auto *array = tt::as_ptr<Array>(srcObj);
            for (size_t i = 0; i < withInputs.size(); ++i) {
                array->set<slot_t>(i, frame->get<slot_t>(dataIndexOf(graph, withInputs[i])));
            }
        } break;
        case TypeCode::Struct: {
            auto *type         = tt::as_ptr<StructType>(srcType);
            auto *st           = tt::as_ptr<Struct>(srcObj);
            const size_t *refs = type->refs();
            for (size_t i = 0; i < withInputs.size(); ++i) {
                st->set<slot_t>(refs[i], frame->get<slot_t>(dataIndexOf(graph, withInputs[i])));
            }
        } break;
        case TypeCode::Function: {
            auto *func         = tt::as_ptr<Function>(srcObj);
            Tuple *closureData = func->tuple();
            ASSERT(closureData != nullptr, "Taskflow FILL function closure is null.");
            for (size_t i = 0; i < withInputs.size(); ++i) {
                closureData->set<slot_t>(i, frame->get<slot_t>(dataIndexOf(graph, withInputs[i])));
            }
        } break;
        default:
            ASSERT(
                false,
                std::format("Unsupported Taskflow FILL type {}.", typeCodeToString(srcCode)));
        }
        frame->set(node->dataIndex, srcObj);
        return toSlot(srcObj);
    }

    case GCNodeKind::Accs: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow ACCS node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const auto *body                = graph->nodeBodyAs<GCAccsBody>(nodeRef);
        if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
            Tuple *tuple = frame->get<Tuple *>(srcIdx);
            ASSERT(body->value < tuple->size(), "Taskflow ACCS tuple index out of bounds.");
            const slot_t value = tuple->get<slot_t>(body->value);
            frame->set(node->dataIndex, value);
            return value;
        }
        Struct *st   = frame->get<Struct *>(srcIdx);
        Type *stType = frame->typeAt<Type>(srcIdx);
        slot_t value = st->get<slot_t>(std::string(body->key()), stType);
        frame->set(node->dataIndex, value);
        return value;
    }

    case GCNodeKind::Func: {
        auto *targetGraph = graph->directCalleeGraphOf(nodeRef);
        ASSERT(targetGraph != nullptr, "Taskflow FUNC target graph is null.");
        Frame *calleeFrame = acquirePreparedRuntimeCallFrame(targetGraph, nodeRef, frame);
        slot_t result      = runPreparedSubgraph(sf, targetGraph, calleeFrame);
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Call: {
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!withInputs.empty(), "Taskflow CALL node must have function input.");
        Function *func    = frame->get<Function *>(dataIndexOf(graph, withInputs.front()));
        auto *targetGraph = func ? func->graph() : nullptr;
        ASSERT(targetGraph != nullptr, "Taskflow CALL target graph is null.");
        Frame *calleeFrame = acquirePreparedRuntimeCallFrame(targetGraph, nodeRef, frame);
        slot_t result      = runPreparedSubgraph(sf, targetGraph, calleeFrame);
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Oper: {
        const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
        const std::string uri(body->uri());
        if (uri == ":mark/map_arr") {
            mark_map_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/apply_arr") {
            mark_apply_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/filter_arr") {
            mark_filter_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/reduce_arr") {
            mark_reduce_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/unordered_reduce_arr") {
            mark_unordered_reduce_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/foreach_arr") {
            mark_foreach_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/unordered_foreach_arr") {
            mark_unordered_foreach_arr(graph, nodeRef, frame, sf);
        } else {
            const slot_t result = executePreparedOperator(graph, nodeRef, frame);
            frame->set(node->dataIndex, result);
            return result;
        }
        return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);
    }

    case GCNodeKind::Brch: {
        const size_t jumpIdx = camel::execute::selectRuntimeBranchArm(graph, nodeRef, frame);
        frame->set(node->dataIndex, static_cast<Int32>(jumpIdx));
        return executePreparedBranchArm(graph, nodeRef, jumpIdx, frame, sf, buildInfo);
    }

    case GCNodeKind::Bind:
    case GCNodeKind::Dref:
        return NullSlot;
    }

    return NullSlot;
}

template <typename FlowT>
void TaskflowExecSchedPass::instantiate_graph_instance_generic(
    FlowT &flowLike, GCGraph *runtimeGraph, Frame *frame) {
    ASSERT(runtimeGraph != nullptr, "Taskflow graph instantiation requires a runtime graph.");
    auto buildInfo = std::make_shared<RuntimeBuildInfo>(buildRuntimeBuildInfo(runtimeGraph));
    std::vector<tf::Task> taskMap(runtimeGraph->nodeBlockCount());
    std::vector<uint8_t> taskBuilt(runtimeGraph->nodeBlockCount(), 0);
    buildRuntimeNodeTasks(flowLike, runtimeGraph, frame, buildInfo, taskMap, taskBuilt);
    connectRuntimeDependencies(flowLike, runtimeGraph, frame, buildInfo, taskMap, taskBuilt);
}

template <typename FlowT>
void TaskflowExecSchedPass::buildRuntimeNodeTasks(
    FlowT &flowLike, GCGraph *graph, Frame *frame,
    const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
    std::vector<uint8_t> &taskBuilt) {
    for (gc_node_ref_t nodeRef : camel::execute::buildReachableExecutionTopoIndices(graph)) {
        const auto *node = requireNode(graph, nodeRef);
        if (node->kind == GCNodeKind::Brch) {
            buildRuntimeBranchJoinRegion(
                flowLike,
                graph,
                frame,
                buildInfo,
                taskMap,
                taskBuilt,
                nodeRef);
            continue;
        }
        if (nodeRef < buildInfo->skipNodes.size() && buildInfo->skipNodes[nodeRef] != 0) {
            continue;
        }
        if (node->kind == GCNodeKind::Bind || node->kind == GCNodeKind::Dref) {
            continue;
        }

        tf::Task task;
        switch (node->kind) {
        case GCNodeKind::Data:
            task = buildDataTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Port:
            task = buildPortTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Copy:
            task = buildCopyTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Cast:
            task = buildCastTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Fill:
            task = buildFillTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Accs:
            task = buildAccsTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Func:
            task = buildFuncTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Call:
            task = buildCallTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Oper:
            task = buildOperTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Sync:
            task = flowLike.emplace([]() {}).name("SYNC");
            break;
        case GCNodeKind::Gate:
            task = flowLike
                       .emplace([graph, nodeRef, frame]() {
                           const auto *node   = requireNode(graph, nodeRef);
                           const slot_t value = readForwardedRuntimeValue(graph, nodeRef, frame);
                           if (node->dataIndex != 0) {
                               frame->set(node->dataIndex, value);
                           }
                       })
                       .name("GATE");
            break;
        case GCNodeKind::Join:
            task = flowLike.emplace([]() {}).name("JOIN");
            break;
        default:
            continue;
        }
        taskMap[nodeRef]   = task;
        taskBuilt[nodeRef] = 1;
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::connectRuntimeDependencies(
    FlowT &flowLike, GCGraph *graph, Frame *frame,
    const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
    std::vector<uint8_t> &taskBuilt) {
    (void)flowLike;
    (void)frame;
    auto addEdges = [&](std::span<const gc_node_ref_t> inputs, tf::Task task) {
        for (gc_node_ref_t inputRef : inputs) {
            if (inputRef >= taskBuilt.size() || taskBuilt[inputRef] == 0) {
                continue;
            }
            if (inputRef < buildInfo->skipNodes.size() && buildInfo->skipNodes[inputRef] != 0 &&
                requireNode(graph, inputRef)->kind != GCNodeKind::Join) {
                continue;
            }
            taskMap[inputRef].precede(task);
        }
    };

    for (gc_node_ref_t nodeRef : camel::execute::buildReachableExecutionTopoIndices(graph)) {
        const auto *node = requireNode(graph, nodeRef);
        if (node->kind == GCNodeKind::Brch) {
            continue;
        }
        if (nodeRef < taskBuilt.size() && taskBuilt[nodeRef] == 0) {
            continue;
        }
        if (nodeRef < buildInfo->skipNodes.size() && buildInfo->skipNodes[nodeRef] != 0) {
            continue;
        }
        addEdges(graph->normInputsOf(nodeRef), taskMap[nodeRef]);
        addEdges(graph->withInputsOf(nodeRef), taskMap[nodeRef]);
        addEdges(graph->ctrlInputsOf(nodeRef), taskMap[nodeRef]);
    }
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildDataTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    (void)graph;
    (void)nodeRef;
    (void)frame;
    return flowLike.emplace([]() {}).name("DATA");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildPortTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    (void)graph;
    (void)nodeRef;
    (void)frame;
    return flowLike.emplace([]() {}).name("PORT");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCopyTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("COPY");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCastTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("CAST");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFillTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("FILL");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildAccsTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("ACCS");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFuncTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("FUNC");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCallTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("CALL");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildOperTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("OPER");
}

template <typename FlowT>
void TaskflowExecSchedPass::buildRuntimeBranchJoinRegion(
    FlowT &flowLike, GCGraph *runtimeGraph, Frame *frame,
    const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
    std::vector<uint8_t> &taskBuilt, gc_node_ref_t brchRuntimeIndex) {
    ASSERT(runtimeGraph != nullptr, "Taskflow BRCH region requires a runtime graph.");
    ASSERT(brchRuntimeIndex < buildInfo->branchArms.size(), "Taskflow BRCH ref is out of range.");
    const auto &arms = buildInfo->branchArms[brchRuntimeIndex];
    ASSERT(!arms.empty(), "Taskflow BRCH region has no branch arms.");
    const gc_node_ref_t joinRef = arms.front().joinIndex;
    ASSERT(joinRef != kInvalidNodeRef, "Taskflow BRCH region has no JOIN.");

    auto selector =
        flowLike
            .emplace([this, runtimeGraph, brchRuntimeIndex, frame](tf::Subflow &sf) {
                const auto brchSlot = dataIndexOf(runtimeGraph, brchRuntimeIndex);
                const size_t jumpIdx =
                    camel::execute::selectRuntimeBranchArm(runtimeGraph, brchRuntimeIndex, frame);
                frame->set(brchSlot, static_cast<Int32>(jumpIdx));
                (void)executePreparedBranchArm(
                    runtimeGraph,
                    brchRuntimeIndex,
                    jumpIdx,
                    frame,
                    sf,
                    nullptr);
            })
            .name("BRCH");

    auto joiner = flowLike
                      .emplace([this, runtimeGraph, joinRef, frame](tf::Subflow &sf) {
                          (void)executePreparedNode(runtimeGraph, joinRef, frame, sf, nullptr);
                      })
                      .name("JOIN");

    auto precedeFromInputs = [&](std::span<const gc_node_ref_t> inputs, tf::Task task) {
        for (gc_node_ref_t inputRef : inputs) {
            if (inputRef < buildInfo->skipNodes.size() && buildInfo->skipNodes[inputRef] != 0 &&
                requireNode(runtimeGraph, inputRef)->kind != GCNodeKind::Join) {
                continue;
            }
            if (inputRef < taskBuilt.size() && taskBuilt[inputRef] != 0) {
                taskMap[inputRef].precede(task);
            }
        }
    };

    precedeFromInputs(runtimeGraph->normInputsOf(brchRuntimeIndex), selector);
    precedeFromInputs(runtimeGraph->ctrlInputsOf(brchRuntimeIndex), selector);

    selector.precede(joiner);

    taskMap[brchRuntimeIndex]   = selector;
    taskBuilt[brchRuntimeIndex] = 1;
    taskMap[joinRef]            = joiner;
    taskBuilt[joinRef]          = 1;
}

void TaskflowExecSchedPass::mark_map_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    Array *arr = frame->get<Array *>(dataIndexOf(graph, graph->normInputsOf(nodeRef).front()));
    Function *func =
        frame->get<Function *>(dataIndexOf(graph, graph->withInputsOf(nodeRef).front()));
    const auto site = makeHigherOrderCallSite(func);
    std::vector<slot_t> results(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
        sf.emplace([this, i, arr, site, &results](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *callee =
                  acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
              results[i] = runPreparedSubgraph(isf, site.runtimeGraph, callee);
          }).name("MAP_ELEM");
    }
    sf.join();
    Array *res = Array::create(camel::core::mm::autoSpace(), arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
        res->set(i, results[i]);
    }
    frame->set(dataIndexOf(graph, nodeRef), res);
}

void TaskflowExecSchedPass::mark_apply_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    Array *arr = frame->get<Array *>(dataIndexOf(graph, graph->normInputsOf(nodeRef).front()));
    Function *func =
        frame->get<Function *>(dataIndexOf(graph, graph->withInputsOf(nodeRef).front()));
    const auto site = makeHigherOrderCallSite(func);
    std::vector<slot_t> results(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
        sf.emplace([this, i, arr, site, &results](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *callee =
                  acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
              results[i] = runPreparedSubgraph(isf, site.runtimeGraph, callee);
          }).name("APPLY_ELEM");
    }
    sf.join();
    for (size_t i = 0; i < arr->size(); ++i) {
        arr->set(i, results[i]);
    }
    frame->set(dataIndexOf(graph, nodeRef), arr);
}

void TaskflowExecSchedPass::mark_filter_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    Array *arr = frame->get<Array *>(dataIndexOf(graph, graph->normInputsOf(nodeRef).front()));
    Function *func =
        frame->get<Function *>(dataIndexOf(graph, graph->withInputsOf(nodeRef).front()));
    const auto site = makeHigherOrderCallSite(func);
    std::vector<bool> keep(arr->size(), false);
    for (size_t i = 0; i < arr->size(); ++i) {
        sf.emplace([this, i, arr, site, &keep](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *callee =
                  acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
              keep[i] = fromSlot<bool>(runPreparedSubgraph(isf, site.runtimeGraph, callee));
          }).name("FILTER_PRED");
    }
    sf.join();
    Array *filtered = Array::create(camel::core::mm::autoSpace(), 0);
    for (size_t i = 0; i < arr->size(); ++i) {
        if (keep[i]) {
            filtered->append(arr->get<slot_t>(i));
        }
    }
    filtered->shrinkToFit();
    frame->set(dataIndexOf(graph, nodeRef), filtered);
}

void TaskflowExecSchedPass::mark_reduce_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    Array *arr = frame->get<Array *>(dataIndexOf(graph, graph->normInputsOf(nodeRef).front()));
    const auto withInputs = graph->withInputsOf(nodeRef);
    Function *func        = frame->get<Function *>(dataIndexOf(graph, withInputs[0]));
    slot_t init           = frame->get<slot_t>(dataIndexOf(graph, withInputs[1]));
    const auto site       = makeHigherOrderCallSite(func);
    if (arr->size() == 0) {
        frame->set(dataIndexOf(graph, nodeRef), init);
        return;
    }
    auto accPtr = std::make_shared<slot_t>(init);
    tf::Task prev;
    bool hasPrev = false;
    for (size_t i = 0; i < arr->size(); ++i) {
        slot_t elem = arr->get<slot_t>(i);
        tf::Task step =
            sf.emplace([this, accPtr, elem, site](tf::Subflow &isf) {
                  std::array<slot_t, 2> args{*accPtr, elem};
                  Frame *callee =
                      acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
                  *accPtr = runPreparedSubgraph(isf, site.runtimeGraph, callee);
              }).name("REDUCE_STEP");
        if (hasPrev) {
            step.succeed(prev);
        }
        prev    = step;
        hasPrev = true;
    }
    sf.join();
    frame->set(dataIndexOf(graph, nodeRef), *accPtr);
}

void TaskflowExecSchedPass::mark_unordered_reduce_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    mark_reduce_arr(graph, nodeRef, frame, sf);
}

void TaskflowExecSchedPass::mark_foreach_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    Array *arr = frame->get<Array *>(dataIndexOf(graph, graph->normInputsOf(nodeRef).front()));
    Function *func =
        frame->get<Function *>(dataIndexOf(graph, graph->withInputsOf(nodeRef).front()));
    const auto site = makeHigherOrderCallSite(func);
    tf::Task prev;
    bool hasPrev = false;
    for (size_t i = 0; i < arr->size(); ++i) {
        slot_t elem = arr->get<slot_t>(i);
        tf::Task step =
            sf.emplace([this, elem, site](tf::Subflow &isf) {
                  std::array<slot_t, 1> args{elem};
                  Frame *callee =
                      acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
                  (void)runPreparedSubgraph(isf, site.runtimeGraph, callee);
              }).name("FOREACH_ELEM");
        if (hasPrev) {
            step.succeed(prev);
        }
        prev    = step;
        hasPrev = true;
    }
    sf.join();
    frame->set(dataIndexOf(graph, nodeRef), NullSlot);
}

void TaskflowExecSchedPass::mark_unordered_foreach_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    Array *arr = frame->get<Array *>(dataIndexOf(graph, graph->normInputsOf(nodeRef).front()));
    Function *func =
        frame->get<Function *>(dataIndexOf(graph, graph->withInputsOf(nodeRef).front()));
    const auto site = makeHigherOrderCallSite(func);
    for (size_t i = 0; i < arr->size(); ++i) {
        sf.emplace([this, i, arr, site](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *callee =
                  acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
              (void)runPreparedSubgraph(isf, site.runtimeGraph, callee);
          }).name("FOREACH_ELEM");
    }
    sf.join();
    frame->set(dataIndexOf(graph, nodeRef), NullSlot);
}

template void TaskflowExecSchedPass::instantiate_graph_instance_generic<tf::Taskflow>(
    tf::Taskflow &flowLike, GCGraph *runtimeGraph, Frame *frame);
