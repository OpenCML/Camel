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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "taskflow.h"
#include "camel/common/algo/topo.h"
#include "camel/compile/gir/nodes.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/module/module.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/debug.h"
#include "camel/utils/log.h"

#include <array>
#include <format>
#include <functional>
#include <queue>
#include <regex>
#include <string>
#include <unordered_set>

using namespace std;
using namespace GIR;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

static Node *resolve_taskflow_value_node(Node *node) {
    Node *current = node;
    while (current != nullptr && current->type() == NodeType::GATE) {
        auto dataInputs = current->dataInputs();
        if (!dataInputs.empty()) {
            current = dataInputs.back();
            continue;
        }
        auto normInputs = current->normInputs();
        if (!normInputs.empty()) {
            current = normInputs.back();
            continue;
        }
        return nullptr;
    }
    return current;
}

static slot_t read_taskflow_node_value(Node *node, Frame *frame) {
    ASSERT(node != nullptr, "Taskflow value read received a null node.");
    Node *valueNode = resolve_taskflow_value_node(node);
    ASSERT(valueNode != nullptr, "Taskflow could not resolve a concrete value node.");
    ASSERT(valueNode->index() != 0, "Taskflow value node resolved to slot 0, which is invalid.");
    return frame->get<slot_t>(valueNode->index());
}

// Read the graph return value while treating GATE as pure forwarding plumbing.
static slot_t get_graph_return(camel::runtime::GCGraph *graph, Frame *frame) {
    return camel::execute::readRuntimeGraphReturn(graph, frame);
}

static camel::runtime::GCGraph *
find_runtime_graph(camel::core::context::Context *context, Graph *graph) {
    if (!context || !graph) {
        return nullptr;
    }
    return context->runtimeGraphManager().find(graph);
}

struct TaskflowHigherOrderCallSite {
    camel::runtime::GCGraph *runtimeGraph = nullptr;
    Tuple *closure                        = nullptr;
};

static TaskflowHigherOrderCallSite make_taskflow_higher_order_call_site(Function *func) {
    auto *runtimeGraph = func ? func->graph() : nullptr;
    ASSERT(
        runtimeGraph != nullptr,
        "Taskflow higher-order call requires a materialized runtime graph target.");
    return {
        .runtimeGraph = runtimeGraph,
        .closure      = func ? func->tuple() : nullptr,
    };
}

static Node *find_taskflow_source_node(
    camel::runtime::GCGraph *runtimeGraph, camel::runtime::gc_node_ref_t nodeRef) {
    if (!runtimeGraph) {
        return nullptr;
    }
    const auto *runtimeNode = runtimeGraph->node(nodeRef);
    auto *sourceGraph       = runtimeGraph->compileGraphMetadata().get();
    if (!runtimeNode || !sourceGraph || runtimeNode->dataIndex == 0) {
        return nullptr;
    }
    auto matches = [runtimeNode](Node *node) {
        return node && node->index() == runtimeNode->dataIndex;
    };
    for (Node *node : sourceGraph->normPorts()) {
        if (matches(node))
            return node;
    }
    for (Node *node : sourceGraph->withPorts()) {
        if (matches(node))
            return node;
    }
    for (Node *node : sourceGraph->closure()) {
        if (matches(node))
            return node;
    }
    for (Node *node : sourceGraph->nodes()) {
        if (matches(node))
            return node;
    }
    if (matches(sourceGraph->outputNode()))
        return sourceGraph->outputNode();
    if (matches(sourceGraph->exitNode()))
        return sourceGraph->exitNode();
    return nullptr;
}

static TaskflowExecSchedPass::GraphInfos::BranchArmMeta
collect_branch_arm_meta(BrchNode *brch, size_t armIndex) {
    ASSERT(brch != nullptr, "Taskflow branch arm collection received a null BRCH node.");
    auto *join = brch->matchedJoin();
    ASSERT(join != nullptr, "Taskflow branch arm collection requires a matched JOIN node.");

    Node *head = brch->armHead(armIndex);
    Node *tail = join->armTail(armIndex);
    ASSERT(head != nullptr && tail != nullptr, "Taskflow branch arm endpoints must be non-null.");

    std::unordered_set<Node *> forward;
    std::vector<Node *> worklist{head};
    while (!worklist.empty()) {
        Node *current = worklist.back();
        worklist.pop_back();
        if (!current || current == join || !forward.insert(current).second) {
            continue;
        }
        for (Node *out : current->ctrlOutputs()) {
            if (&out->graph() == &brch->graph() && out != join) {
                worklist.push_back(out);
            }
        }
        for (Node *out : current->normOutputs()) {
            if (&out->graph() == &brch->graph() && out != join) {
                worklist.push_back(out);
            }
        }
        for (Node *out : current->withOutputs()) {
            if (&out->graph() == &brch->graph() && out != join) {
                worklist.push_back(out);
            }
        }
    }

    std::vector<Node *> topoNodes;
    std::unordered_set<Node *> visited;
    std::function<void(Node *)> visit = [&](Node *node) {
        if (!node || !forward.contains(node) || !visited.insert(node).second) {
            return;
        }
        for (Node *in : node->ctrlInputs()) {
            if (&in->graph() == &brch->graph() && forward.contains(in)) {
                visit(in);
            }
        }
        for (Node *in : node->dataInputs()) {
            if (&in->graph() == &brch->graph() && forward.contains(in)) {
                visit(in);
            }
        }
        topoNodes.push_back(node);
    };
    visit(tail);
    ASSERT(
        !topoNodes.empty(),
        std::format(
            "Taskflow branch arm collection produced an empty region for graph '{}' arm {}.",
            brch->graph().name(),
            armIndex));

    return {
        .head                   = head,
        .tail                   = tail,
        .headRuntimeIndex       = camel::runtime::kInvalidNodeRef,
        .tailRuntimeIndex       = camel::runtime::kInvalidNodeRef,
        .topoNodes              = std::move(topoNodes),
        .topoNodeRuntimeIndices = {},
    };
}

static std::vector<Node *> collect_taskflow_execution_nodes(Graph *graph) {
    ASSERT(graph != nullptr, "Taskflow execution-node collection received a null graph.");

    std::vector<Node *> nodes;
    nodes.reserve(graph->nodes().size() + 2);
    for (Node *node : graph->nodes()) {
        nodes.push_back(node);
    }

    auto push_unique = [&](Node *node) {
        if (node && std::find(nodes.begin(), nodes.end(), node) == nodes.end()) {
            nodes.push_back(node);
        }
    };

    // Some rewritten graphs keep the executable output anchor outside graph->nodes().
    // Runtime schedulers must still see that carrier node, otherwise the entire
    // graph can become observationally empty after a rewrite such as std::inline.
    push_unique(graph->outputNode());
    push_unique(graph->exitNode());
    return nodes;
}

static void populate_graph_info(
    TaskflowExecSchedPass &pass, Graph *graph, camel::runtime::GCGraph *runtimeGraph) {
    ASSERT(graph != nullptr, "Taskflow graph metadata population received a null graph.");

    auto &gt        = runtimeGraph ? pass.globalBuildCtx_.getOrCreateGraphInfos(runtimeGraph, graph)
                                   : pass.globalBuildCtx_.getOrCreateGraphInfos(graph);
    gt.graph        = graph;
    gt.runtimeGraph = runtimeGraph;
    gt.joinToBrch.clear();
    gt.branchArms.clear();
    gt.nodeExecMeta.clear();
    gt.runtimeNodeExecMeta.clear();
    gt.runtimeBranchArms.clear();
    gt.normPortIndices.clear();
    gt.withPortIndices.clear();
    gt.closureIndices.clear();
    gt.runtimeSkipNodeIndices.clear();

    gt.normPortIndices.reserve(graph->normPorts().size());
    for (Node *port : graph->normPorts())
        gt.normPortIndices.push_back(port->index());

    gt.withPortIndices.reserve(graph->withPorts().size());
    for (Node *port : graph->withPorts())
        gt.withPortIndices.push_back(port->index());

    gt.closureIndices.reserve(graph->closure().size());
    for (Node *closure : graph->closure())
        gt.closureIndices.push_back(closure->index());

    std::unordered_map<GIR::data_idx_t, camel::runtime::gc_node_ref_t> runtimeNodeIndexByData;
    if (runtimeGraph != nullptr) {
        gt.runtimeNodeExecMeta.resize(runtimeGraph->nodeBlockCount());
        gt.runtimeBranchArms.resize(runtimeGraph->nodeBlockCount());
        runtimeNodeIndexByData.reserve(runtimeGraph->nodeCount());
        for (auto it = runtimeGraph->nodes().begin(); it != runtimeGraph->nodes().end(); ++it) {
            const auto nodeRef           = it.ref();
            auto &runtimeMeta            = gt.runtimeNodeExecMeta[nodeRef];
            runtimeMeta.runtimeNodeIndex = nodeRef;
            runtimeMeta.normIndices.clear();
            runtimeMeta.withIndices.clear();
            for (auto inputIndex : runtimeGraph->normInputsOf(nodeRef)) {
                const auto *inputRecord = runtimeGraph->node(inputIndex);
                ASSERT(inputRecord != nullptr, "Taskflow runtime norm-input record is missing.");
                runtimeMeta.normIndices.push_back(
                    static_cast<GIR::data_idx_t>(inputRecord->dataIndex));
            }
            for (auto inputIndex : runtimeGraph->withInputsOf(nodeRef)) {
                const auto *inputRecord = runtimeGraph->node(inputIndex);
                ASSERT(inputRecord != nullptr, "Taskflow runtime with-input record is missing.");
                runtimeMeta.withIndices.push_back(
                    static_cast<GIR::data_idx_t>(inputRecord->dataIndex));
            }
            const auto *runtimeNode = runtimeGraph->node(nodeRef);
            if (runtimeNode != nullptr && runtimeNode->dataIndex != 0) {
                runtimeNodeIndexByData.emplace(runtimeNode->dataIndex, nodeRef);
            }
        }
    }

    for (Node *n : collect_taskflow_execution_nodes(graph)) {
        auto &nodeMeta = gt.getOrCreateNodeExecMeta(n);
        nodeMeta.normIndices.clear();
        nodeMeta.withIndices.clear();
        nodeMeta.runtimeNodeIndex = camel::runtime::kInvalidNodeRef;
        nodeMeta.normIndices.reserve(n->normInputs().size());
        nodeMeta.withIndices.reserve(n->withInputs().size());
        for (Node *in : n->normInputs())
            nodeMeta.normIndices.push_back(in->index());
        for (Node *in : n->withInputs())
            nodeMeta.withIndices.push_back(in->index());
        if (auto it = runtimeNodeIndexByData.find(n->index()); it != runtimeNodeIndexByData.end()) {
            nodeMeta.runtimeNodeIndex = it->second;
        }

        if (n->type() == NodeType::BRCH) {
            const auto candidates = n->ctrlOutputs();
            Node *join            = n->normOutputs().front();
            pass.globalBuildCtx_.skipNodes.insert(join);
            ASSERT(join->type() == NodeType::JOIN, "BRCH must be paired with JOIN.");
            gt.joinToBrch[join] = n;
            auto *brch          = tt::as_ptr<BrchNode>(n);
            auto &arms          = gt.branchArms[n];
            arms.reserve(candidates.size());
            for (size_t armIndex = 0; armIndex < candidates.size(); ++armIndex) {
                auto arm = collect_branch_arm_meta(brch, armIndex);
                if (runtimeGraph != nullptr) {
                    auto runtimeArm = camel::execute::collectRuntimeBranchArmRegion(
                        runtimeGraph,
                        runtimeNodeIndexByData.at(n->index()),
                        armIndex);
                    arm.headRuntimeIndex       = runtimeArm.headIndex;
                    arm.tailRuntimeIndex       = runtimeArm.tailIndex;
                    arm.topoNodeRuntimeIndices = std::move(runtimeArm.topoIndices);
                    gt.runtimeBranchArms[runtimeNodeIndexByData.at(n->index())].push_back(arm);
                }
                for (Node *armNode : arm.topoNodes) {
                    pass.globalBuildCtx_.skipNodes.insert(armNode);
                    if (runtimeGraph != nullptr) {
                        gt.runtimeSkipNodeIndices.insert(
                            runtimeNodeIndexByData.at(armNode->index()));
                    }
                }
                arms.push_back(std::move(arm));
            }
            if (runtimeGraph != nullptr) {
                const auto joinRuntimeIndex = runtimeNodeIndexByData.at(join->index());
                gt.runtimeSkipNodeIndices.insert(joinRuntimeIndex);
            }
        }
    }

    if (runtimeGraph != nullptr) {
        pass.framePool_.warmup(runtimeGraph, 1);
    } else {
        pass.framePool_.warmup(graph, 1);
    }
}

static const TaskflowExecSchedPass::GraphInfos::NodeExecMeta &taskflow_node_exec_meta(
    const TaskflowExecSchedPass::GlobalBuildCtx &buildCtx, Frame *frame, Node *node) {
    ASSERT(node != nullptr, "Taskflow node exec meta lookup received a null node.");
    if (frame != nullptr && frame->runtimeGraph() != nullptr) {
        const auto &sourceMeta = buildCtx.getGraphInfos(&node->graph()).getNodeExecMeta(node);
        if (sourceMeta.runtimeNodeIndex != camel::runtime::kInvalidNodeRef) {
            return buildCtx.getGraphInfos(frame->runtimeGraph())
                .getNodeExecMeta(sourceMeta.runtimeNodeIndex);
        }
    }
    return buildCtx.getGraphInfos(&node->graph()).getNodeExecMeta(node);
}

static bool taskflow_should_skip_node(
    const TaskflowExecSchedPass::GlobalBuildCtx &buildCtx, Frame *frame, Node *node) {
    ASSERT(node != nullptr, "Taskflow skip-node query received a null node.");
    if (frame != nullptr && frame->runtimeGraph() != nullptr) {
        const auto &sourceMeta = buildCtx.getGraphInfos(&node->graph()).getNodeExecMeta(node);
        if (sourceMeta.runtimeNodeIndex != camel::runtime::kInvalidNodeRef) {
            const auto &runtimeInfo = buildCtx.getGraphInfos(frame->runtimeGraph());
            return runtimeInfo.runtimeSkipNodeIndices.contains(sourceMeta.runtimeNodeIndex);
        }
    }
    return buildCtx.skipNodes.contains(node);
}

static const std::vector<TaskflowExecSchedPass::GraphInfos::BranchArmMeta> &taskflow_branch_arms(
    const TaskflowExecSchedPass::GlobalBuildCtx &buildCtx, Frame *frame, Node *brch) {
    ASSERT(brch != nullptr, "Taskflow branch-arm lookup requires a BRCH node.");
    if (frame != nullptr && frame->runtimeGraph() != nullptr) {
        const auto &sourceMeta = buildCtx.getGraphInfos(&brch->graph()).getNodeExecMeta(brch);
        if (sourceMeta.runtimeNodeIndex != camel::runtime::kInvalidNodeRef) {
            const auto &runtimeInfo = buildCtx.getGraphInfos(frame->runtimeGraph());
            if (sourceMeta.runtimeNodeIndex < runtimeInfo.runtimeBranchArms.size() &&
                !runtimeInfo.runtimeBranchArms[sourceMeta.runtimeNodeIndex].empty()) {
                return runtimeInfo.runtimeBranchArms[sourceMeta.runtimeNodeIndex];
            }
        }
    }
    return buildCtx.getGraphInfos(&brch->graph()).branchArms.at(brch);
}

graph_ptr_t TaskflowExecSchedPass::apply(camel::runtime::GCGraph *graph, std::ostream & /*os*/) {
    ASSERT(graph != nullptr, "Taskflow requires a non-null runtime root graph.");
    buildGraphsInfo(graph);

    Frame *rootFrame = framePool_.acquire(graph);
    try {
        slot_t result = evalGraphTF(graph, rootFrame);
        context_->captureProcessExitCode(graph, result);
        framePool_.release(rootFrame);
    } catch (...) {
        framePool_.release(rootFrame);
        throw;
    }

    return Graph::null();
}

slot_t TaskflowExecSchedPass::evalGraphTF(camel::runtime::GCGraph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow evaluation requires a materialized runtime graph.");
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("Taskflow", "Evaluating graph: {}", graph->name()));
    mainFlow_.clear();
    instantiate_graph_instance_generic(mainFlow_, graph, frame);
    executor_.run(mainFlow_).wait();
    slot_t ret = get_graph_return(graph, frame);
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("Taskflow", "Graph {} finished.", graph->name()));
    return ret;
}

slot_t TaskflowExecSchedPass::runPreparedSubgraph(
    tf::Subflow &sf, camel::runtime::GCGraph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow prepared subgraph execution requires a runtime graph.");
    (void)sf;
    try {
        tf::Taskflow localFlow;
        instantiate_graph_instance_generic(localFlow, graph, frame);
        executor_.run(localFlow).wait();
        slot_t result = get_graph_return(graph, frame);
        framePool_.release(frame);
        return result;
    } catch (...) {
        framePool_.release(frame);
        throw;
    }
}

Frame *TaskflowExecSchedPass::acquirePreparedNodeCallFrame(
    camel::runtime::GCGraph *targetGraph, Node *callNode, Frame *sourceFrame) {
    ASSERT(targetGraph != nullptr, "Taskflow call target graph is null.");
    ASSERT(sourceFrame != nullptr, "Taskflow source frame is null.");
    ASSERT(
        sourceFrame->runtimeGraph() != nullptr,
        "Taskflow call binding requires a runtime caller graph.");
    Frame *dest          = framePool_.acquire(targetGraph);
    const auto &callMeta = taskflow_node_exec_meta(globalBuildCtx_, sourceFrame, callNode);
    ASSERT(
        callMeta.runtimeNodeIndex != camel::runtime::kInvalidNodeRef,
        "Taskflow runtime node index is missing for call-site frame binding.");

    if (callNode->type() == NodeType::CALL) {
        camel::execute::fillFrameForIndirectCall(
            sourceFrame,
            dest,
            sourceFrame->runtimeGraph(),
            callMeta.runtimeNodeIndex);
    } else {
        camel::execute::fillFrameForDirectInvoke(
            sourceFrame,
            dest,
            sourceFrame->runtimeGraph(),
            callMeta.runtimeNodeIndex);
    }

    return dest;
}

Frame *TaskflowExecSchedPass::acquirePreparedRuntimeCallFrame(
    camel::runtime::GCGraph *targetGraph, camel::runtime::gc_node_ref_t callNodeRuntimeIndex,
    Frame *sourceFrame) {
    ASSERT(targetGraph != nullptr, "Taskflow runtime call target graph is null.");
    ASSERT(sourceFrame != nullptr, "Taskflow runtime source frame is null.");
    ASSERT(
        sourceFrame->runtimeGraph() != nullptr,
        "Taskflow runtime call binding requires a runtime caller graph.");
    Frame *dest            = framePool_.acquire(targetGraph);
    const auto *callRecord = sourceFrame->runtimeGraph()->node(callNodeRuntimeIndex);
    ASSERT(callRecord != nullptr, "Taskflow runtime call-site record is missing.");

    if (callRecord->kind == camel::runtime::GCNodeKind::Call) {
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
    camel::runtime::GCGraph *targetGraph, Tuple *closure, std::span<const slot_t> args) {
    ASSERT(
        targetGraph != nullptr,
        "Taskflow closure call requires a materialized runtime graph target.");
    Frame *dest                 = framePool_.acquire(targetGraph);
    const auto &targetInfo      = globalBuildCtx_.getGraphInfos(targetGraph);
    const auto &normPortIndices = targetInfo.normPortIndices;
    const auto &closureIndices  = targetInfo.closureIndices;

    ASSERT(
        args.size() == normPortIndices.size(),
        "Norm args and ports count mismatch in Taskflow closure call frame fill.");
    ASSERT(
        targetInfo.withPortIndices.empty(),
        "Closure call target graph should not require with ports in Taskflow.");
    ASSERT(
        closure->size() == closureIndices.size(),
        "Closure nodes and tuple size mismatch in Taskflow closure call frame fill.");

    for (size_t i = 0; i < args.size(); ++i)
        dest->set(normPortIndices[i], args[i]);
    for (size_t j = 0; j < closure->size(); ++j)
        dest->set(closureIndices[j], closure->get<slot_t>(j));

    return dest;
}

slot_t TaskflowExecSchedPass::executePreparedOperator(Node *n, Frame *frame) {
    auto opNode     = tt::as_ptr<OperNode>(n);
    const auto &uri = opNode->oper()->uri();
    auto opFunc     = context_->execMgr().find(uri);
    if (!opFunc) {
        throw reportRuntimeFault(
            *context_,
            RuntimeFault::make(RuntimeDiag::UnrecognizedOperatorURI, uri),
            makeNodeExecutionSite(
                context_->sourceContext(),
                frame ? frame->runtimeGraph() : nullptr,
                &n->graph(),
                n,
                0,
                "taskflow",
                ExecutionSiteKind::TaskNode));
    }

    const auto &meta = taskflow_node_exec_meta(globalBuildCtx_, frame, n);
    data_arr_t nargs{meta.normIndices.data(), static_cast<size_t>(meta.normIndices.size())};
    data_arr_t wargs{meta.withIndices.data(), static_cast<size_t>(meta.withIndices.size())};
    FrameArgsView withView(*frame, wargs);
    FrameArgsView normView(*frame, nargs);

    try {
        return (*opFunc)(withView, normView, *context_);
    } catch (const RuntimeFault &fault) {
        throw reportRuntimeFault(
            *context_,
            fault,
            makeNodeExecutionSite(
                context_->sourceContext(),
                frame ? frame->runtimeGraph() : nullptr,
                &n->graph(),
                n,
                0,
                "taskflow",
                ExecutionSiteKind::TaskNode));
    }
}

void TaskflowExecSchedPass::buildGraphsInfo(Graph *rootGraph) {
    globalBuildCtx_.ownedGraphInfos.clear();
    globalBuildCtx_.graphInfoMap.clear();
    globalBuildCtx_.runtimeGraphInfoMap.clear();
    globalBuildCtx_.skipNodes.clear();
    std::unordered_set<Graph *> visited;
    std::queue<Graph *> q;
    q.push(rootGraph);

    while (!q.empty()) {
        auto g = q.front();
        q.pop();
        if (visited.count(g))
            continue;
        visited.insert(g);

        populate_graph_info(*this, g, find_runtime_graph(context_.get(), g));

        for (Node *n : collect_taskflow_execution_nodes(g)) {
            if (n->type() == NodeType::FUNC) {
                auto fn    = tt::as_ptr<FuncNode>(n);
                Graph *sub = fn->bodyGraph();
                if (!visited.count(sub))
                    q.push(sub);
            }
        }

        for (const auto &[_, graphsSet] : g->subGraphs()) {
            for (const auto &sg : graphsSet) {
                if (!visited.count(sg.get()))
                    q.push(sg.get());
            }
        }

        // Sealed graphs already own finalized frame layouts. Warm only the frame pool here so the
        // first subgraph entry does not pay the cold-start cost.
    }
}

slot_t TaskflowExecSchedPass::executePreparedNode(Node *n, Frame *frame, tf::Subflow &sf) {
    ASSERT(n != nullptr, "Taskflow prepared node execution received a null node.");

    switch (n->type()) {
    case NodeType::DATA:
    case NodeType::PORT:
        return frame->get<slot_t>(n->index());

    case NodeType::SYNC:
        return frame->get<slot_t>(n->index());

    case NodeType::GATE: {
        auto dataInputs = n->dataInputs();
        if (!dataInputs.empty()) {
            slot_t value = read_taskflow_node_value(dataInputs.back(), frame);
            if (n->index() != 0) {
                frame->set(n->index(), value);
            }
            return value;
        }
        return n->index() == 0 ? NullSlot : frame->get<slot_t>(n->index());
    }

    case NodeType::COPY: {
        auto inputNode    = n->normInputs().front();
        data_idx_t srcIdx = inputNode->index();
        TypeCode srcCode  = frame->codeAt(srcIdx);
        if (isGCTraced(srcCode)) {
            Object *srcData  = frame->get<Object *>(srcIdx);
            Type *srcTypePtr = frame->typeAt<Type>(srcIdx);
            auto *copy       = srcData->clone(mm::autoSpace(), srcTypePtr, false);
            frame->set(n->index(), copy);
            return toSlot(copy);
        }
        slot_t value = frame->get<slot_t>(srcIdx);
        frame->set(n->index(), value);
        return value;
    }

    case NodeType::CAST: {
        auto inputNode = n->normInputs().front();
        Type *srcType  = frame->typeAt<Type>(inputNode->index());
        Type *tgtType  = n->dataType();
        slot_t value   = frame->get<slot_t>(inputNode->index());
        slot_t result  = tgtType->castSlotFrom(value, srcType);
        frame->set(n->index(), result);
        return result;
    }

    case NodeType::FILL: {
        auto srcNode           = n->normInputs().front();
        const auto &dataInputs = n->withInputs();
        TypeCode srcCode       = frame->codeAt(srcNode->index());
        Type *srcType          = frame->typeAt<Type>(srcNode->index());
        ASSERT(isGCTraced(srcCode), "FILL target type is not GC-traced in Taskflow.");
        Object *srcObj =
            frame->get<Object *>(srcNode->index())->clone(mm::autoSpace(), srcType, false);
        ASSERT(srcObj != nullptr, "FILL target data is null.");
        switch (srcCode) {
        case TypeCode::Tuple: {
            auto type          = tt::as_ptr<TupleType>(srcType);
            auto tup           = tt::as_ptr<Tuple>(srcObj);
            const size_t *refs = type->refs();
            for (size_t j = 0; j < dataInputs.size(); ++j)
                tup->set<slot_t>(refs[j], frame->get<slot_t>(dataInputs[j]->index()));
        } break;
        case TypeCode::Array: {
            auto arr = tt::as_ptr<Array>(srcObj);
            for (size_t j = 0; j < dataInputs.size(); ++j)
                arr->set<slot_t>(j, frame->get<slot_t>(dataInputs[j]->index()));
        } break;
        case TypeCode::Struct: {
            auto type          = tt::as_ptr<StructType>(srcType);
            auto str           = tt::as_ptr<Struct>(srcObj);
            const size_t *refs = type->refs();
            for (size_t j = 0; j < dataInputs.size(); ++j)
                str->set<slot_t>(refs[j], frame->get<slot_t>(dataInputs[j]->index()));
        } break;
        case TypeCode::Function: {
            auto func          = tt::as_ptr<Function>(srcObj);
            Tuple *closureData = func->tuple();
            for (size_t j = 0; j < dataInputs.size(); ++j)
                closureData->set<slot_t>(j, frame->get<slot_t>(dataInputs[j]->index()));
        } break;
        default:
            ASSERT(
                false,
                std::format(
                    "Unsupported FILL target type {} in Taskflow.",
                    typeCodeToString(srcCode)));
        }
        frame->set(n->index(), srcObj);
        return toSlot(srcObj);
    }

    case NodeType::ACCS: {
        auto accsNode     = tt::as_ptr<AccsNode>(n);
        data_idx_t srcIdx = n->dataInputs().front()->index();
        if (accsNode->isNum()) {
            size_t idx = accsNode->numIndex();
            Tuple *t   = frame->get<Tuple *>(srcIdx);
            ASSERT(idx < t->size(), "Tuple index out of bounds in Taskflow.");
            slot_t value = t->get<slot_t>(idx);
            frame->set(n->index(), value);
            return value;
        }
        std::string key  = accsNode->strIndex();
        Struct *s        = frame->get<Struct *>(srcIdx);
        Type *structType = frame->typeAt<Type>(srcIdx);
        slot_t value     = s->get<slot_t>(key, structType);
        frame->set(n->index(), value);
        return value;
    }

    case NodeType::FUNC: {
        const auto &nodeMeta = taskflow_node_exec_meta(globalBuildCtx_, frame, n);
        auto *runtimeTarget =
            frame->runtimeGraph()
                ? frame->runtimeGraph()->directCalleeGraphOf(nodeMeta.runtimeNodeIndex)
                : nullptr;
        ASSERT(
            runtimeTarget != nullptr,
            "Taskflow direct FUNC target must have a materialized runtime graph.");
        Frame *funcFrame =
            acquirePreparedRuntimeCallFrame(runtimeTarget, nodeMeta.runtimeNodeIndex, frame);
        slot_t result = runPreparedSubgraph(sf, runtimeTarget, funcFrame);
        frame->set(n->index(), result);
        return result;
    }

    case NodeType::CALL: {
        auto *callNode      = tt::as_ptr<CallNode>(n);
        auto *runtimeTarget = frame->get<Function *>(callNode->calleeInput()->index())->graph();
        ASSERT(
            runtimeTarget != nullptr,
            "Taskflow indirect CALL requires a materialized runtime graph target.");
        const auto &nodeMeta = taskflow_node_exec_meta(globalBuildCtx_, frame, n);
        Frame *funcFrame =
            acquirePreparedRuntimeCallFrame(runtimeTarget, nodeMeta.runtimeNodeIndex, frame);
        slot_t result = runPreparedSubgraph(sf, runtimeTarget, funcFrame);
        frame->set(n->index(), result);
        return result;
    }

    case NodeType::OPER: {
        slot_t result = executePreparedOperator(n, frame);
        frame->set(n->index(), result);
        return result;
    }

    case NodeType::BRCH: {
        auto *brch           = tt::as_ptr<BrchNode>(n);
        const auto &nodeMeta = taskflow_node_exec_meta(globalBuildCtx_, frame, n);
        const auto &normIns  = nodeMeta.normIndices;
        const auto &withIns  = nodeMeta.withIndices;
        ASSERT(normIns.size() == 1, "Branch node must have exactly one norm input.");

        size_t jumpIdx = 0;
        if (withIns.empty()) {
            bool cond = frame->get<bool>(normIns.front());
            jumpIdx   = cond ? 0 : 1;
        } else {
            TypeCode condType = frame->codeAt(normIns.front());
            size_t j          = 0;
            if (isGCTraced(condType)) {
                Type *condTypePtr = frame->typeAt<Type>(normIns.front());
                Object *condData  = frame->get<Object *>(normIns.front());
                for (; j < withIns.size(); ++j) {
                    Object *caseData = frame->get<Object *>(withIns[j]);
                    if (condData->equals(caseData, condTypePtr, false)) {
                        jumpIdx = j;
                        break;
                    }
                }
            } else {
                slot_t condData = frame->get<slot_t>(normIns.front());
                for (; j < withIns.size(); ++j) {
                    if (condData == frame->get<slot_t>(withIns[j])) {
                        jumpIdx = j;
                        break;
                    }
                }
            }
            if (j == withIns.size()) {
                jumpIdx = withIns.size();
            }
        }
        frame->set(brch->index(), static_cast<Int32>(jumpIdx));
        return executePreparedBranchArm(brch, jumpIdx, frame, sf);
    }

    case NodeType::JOIN:
        return frame->get<slot_t>(n->index());

    default:
        ASSERT(false, std::format("Unsupported prepared Taskflow node type: {}", n->toString()));
        return NullSlot;
    }
}

slot_t TaskflowExecSchedPass::executePreparedBranchArm(
    BrchNode *brch, size_t armIndex, Frame *frame, tf::Subflow &sf) {
    ASSERT(brch != nullptr, "Taskflow branch arm execution received a null BRCH node.");
    const auto &arms = taskflow_branch_arms(globalBuildCtx_, frame, brch);
    ASSERT(armIndex < arms.size(), "Taskflow branch arm index is out of range.");

    const auto &arm = arms[armIndex];
    if (frame != nullptr && frame->runtimeGraph() != nullptr &&
        !arm.topoNodeRuntimeIndices.empty()) {
        for (camel::runtime::gc_node_ref_t runtimeIndex : arm.topoNodeRuntimeIndices) {
            Node *node = find_taskflow_source_node(frame->runtimeGraph(), runtimeIndex);
            ASSERT(node != nullptr, "Taskflow runtime branch node is missing source metadata.");
            executePreparedNode(node, frame, sf);
        }
    } else {
        for (Node *node : arm.topoNodes) {
            executePreparedNode(node, frame, sf);
        }
    }

    auto *join = brch->matchedJoin();
    ASSERT(join != nullptr, "Taskflow branch execution requires a matched JOIN node.");
    slot_t result = NullSlot;
    if (frame != nullptr && frame->runtimeGraph() != nullptr &&
        arm.tailRuntimeIndex != camel::runtime::kInvalidNodeRef) {
        const auto *tailRecord = frame->runtimeGraph()->node(arm.tailRuntimeIndex);
        ASSERT(tailRecord != nullptr, "Taskflow runtime branch tail record is missing.");
        result = frame->get<slot_t>(tailRecord->dataIndex);
    } else {
        result = read_taskflow_node_value(arm.tail, frame);
    }
    frame->set(join->index(), result);
    return result;
}

void TaskflowExecSchedPass::buildGraphsInfo(camel::runtime::GCGraph *rootGraph) {
    globalBuildCtx_.ownedGraphInfos.clear();
    globalBuildCtx_.graphInfoMap.clear();
    globalBuildCtx_.runtimeGraphInfoMap.clear();
    globalBuildCtx_.skipNodes.clear();

    for (camel::runtime::GCGraph *runtimeGraph :
         camel::runtime::collectReachableGraphs(rootGraph)) {
        ASSERT(runtimeGraph != nullptr, "Taskflow reachable runtime graph set contains null.");
        Graph *graph = runtimeGraph->compileGraphMetadata().get();
        ASSERT(graph != nullptr, "Taskflow runtime graph is missing compile graph metadata.");
        populate_graph_info(*this, graph, runtimeGraph);
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::instantiate_graph_instance_generic(
    FlowT &flowLike, camel::runtime::GCGraph *runtimeGraph, Frame *frame) {
    ASSERT(runtimeGraph != nullptr, "Taskflow graph instantiation requires a runtime graph.");
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "Taskflow",
        "Instantiating runtime graph instance: {} (runtimeNodes={})",
        runtimeGraph->name(),
        runtimeGraph->nodeBlockCount()));
    std::vector<tf::Task> taskMap(runtimeGraph->nodeBlockCount());
    std::vector<uint8_t> taskBuilt(runtimeGraph->nodeBlockCount(), 0);
    buildRuntimeNodeTasks(flowLike, runtimeGraph, frame, taskMap, taskBuilt);
    connectRuntimeDependencies(flowLike, runtimeGraph, frame, taskMap, taskBuilt);
}

template <typename FlowT>
void TaskflowExecSchedPass::instantiate_graph_instance_generic(
    FlowT &flowLike, Graph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow graph instantiation requires a source graph.");
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "Taskflow",
        "Instantiating graph instance: {} (nodes={})",
        graph->name(),
        graph->nodes().size()));
    std::unordered_map<Node *, tf::Task> taskMap;
    buildNormalNodeTasks(flowLike, graph, frame, taskMap);
    connectDependencies(flowLike, graph, frame, taskMap);
}

template <typename FlowT>
void TaskflowExecSchedPass::buildRuntimeNodeTasks(
    FlowT &flowLike, camel::runtime::GCGraph *runtimeGraph, Frame *frame,
    std::vector<tf::Task> &taskMap, std::vector<uint8_t> &taskBuilt) {
    ASSERT(runtimeGraph != nullptr, "Taskflow runtime-node task construction requires a graph.");
    ASSERT(
        taskMap.size() == runtimeGraph->nodeBlockCount() &&
            taskBuilt.size() == runtimeGraph->nodeBlockCount(),
        "Taskflow runtime task tables must match runtime graph node count.");
    for (camel::runtime::gc_node_ref_t runtimeIndex :
         camel::execute::buildReachableExecutionTopoIndices(runtimeGraph)) {
        const auto *record = runtimeGraph->node(runtimeIndex);
        ASSERT(record != nullptr, "Taskflow runtime node record lookup failed.");
        Node *n = find_taskflow_source_node(runtimeGraph, runtimeIndex);
        if (n == nullptr) {
            continue;
        }
        if (taskflow_should_skip_node(globalBuildCtx_, frame, n)) {
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "Taskflow",
                "Skipping runtime node (BRCH region) graph={}: {}",
                runtimeGraph->name(),
                n->toString()));
            continue;
        }
        tf::Task t;
        switch (record->kind) {
        case camel::runtime::GCNodeKind::Data:
            t = buildDataTask(flowLike, n, frame);
            break;
        case camel::runtime::GCNodeKind::Port:
            t = buildPortTask(flowLike, n, frame);
            break;
        case camel::runtime::GCNodeKind::Copy:
            t = buildCopyTask(flowLike, n, frame);
            break;
        case camel::runtime::GCNodeKind::Cast:
            t = buildCastTask(flowLike, n, frame);
            break;
        case camel::runtime::GCNodeKind::Fill:
            t = buildFillTask(flowLike, n, frame);
            break;
        case camel::runtime::GCNodeKind::Accs:
            t = buildAccsTask(flowLike, n, frame);
            break;
        case camel::runtime::GCNodeKind::Func:
            t = buildFuncTask(flowLike, n, frame);
            break;
        case camel::runtime::GCNodeKind::Call:
            t = buildCallTask(flowLike, n, frame);
            break;
        case camel::runtime::GCNodeKind::Oper:
            t = buildOperTask(flowLike, n, frame);
            break;
        case camel::runtime::GCNodeKind::Sync:
        case camel::runtime::GCNodeKind::Gate:
            if (record->kind == camel::runtime::GCNodeKind::Gate) {
                t = flowLike
                        .emplace([n, frame]() {
                            auto dataInputs = n->dataInputs();
                            if (!dataInputs.empty()) {
                                slot_t value = read_taskflow_node_value(dataInputs.back(), frame);
                                if (n->index() != 0) {
                                    frame->set(n->index(), value);
                                }
                            }
                        })
                        .name("GATE");
            } else {
                t = flowLike.emplace([]() {}).name("SYNC");
            }
            break;
        case camel::runtime::GCNodeKind::Brch:
            buildRuntimeBranchJoinRegion(
                flowLike,
                runtimeGraph,
                frame,
                taskMap,
                taskBuilt,
                runtimeIndex,
                n);
            continue;
        case camel::runtime::GCNodeKind::Join:
        case camel::runtime::GCNodeKind::Bind:
        case camel::runtime::GCNodeKind::Dref:
            continue;
        default:
            ASSERT(
                false,
                std::format(
                    "Unsupported runtime node kind in Taskflow task construction: {}",
                    static_cast<int>(record->kind)));
        }
        taskMap[runtimeIndex]   = t;
        taskBuilt[runtimeIndex] = 1;
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::connectRuntimeDependencies(
    FlowT &flowLike, camel::runtime::GCGraph *runtimeGraph, Frame *frame,
    std::vector<tf::Task> &taskMap, std::vector<uint8_t> &taskBuilt) {
    (void)flowLike;
    ASSERT(runtimeGraph != nullptr, "Taskflow runtime dependency construction requires a graph.");
    const auto &graphInfo              = globalBuildCtx_.getGraphInfos(runtimeGraph);
    auto add_edges_from_runtime_inputs = [&](std::span<const camel::runtime::gc_node_ref_t> inputs,
                                             tf::Task tsk) {
        for (uint32_t inputIndex : inputs) {
            const auto *inputRecord = runtimeGraph->node(inputIndex);
            if (inputRecord == nullptr) {
                continue;
            }
            if (graphInfo.runtimeSkipNodeIndices.contains(inputIndex) &&
                inputRecord->kind != camel::runtime::GCNodeKind::Join) {
                continue;
            }
            if (inputIndex < taskBuilt.size() && taskBuilt[inputIndex] != 0) {
                taskMap[inputIndex].precede(tsk);
            }
        }
    };

    for (camel::runtime::gc_node_ref_t runtimeIndex :
         camel::execute::buildReachableExecutionTopoIndices(runtimeGraph)) {
        const auto *record = runtimeGraph->node(runtimeIndex);
        if (record == nullptr) {
            continue;
        }
        if (record->kind == camel::runtime::GCNodeKind::Brch) {
            continue;
        }
        if (graphInfo.runtimeSkipNodeIndices.contains(runtimeIndex)) {
            continue;
        }
        if (runtimeIndex >= taskBuilt.size() || taskBuilt[runtimeIndex] == 0) {
            continue;
        }
        add_edges_from_runtime_inputs(
            runtimeGraph->normInputsOf(runtimeIndex),
            taskMap[runtimeIndex]);
        add_edges_from_runtime_inputs(
            runtimeGraph->withInputsOf(runtimeIndex),
            taskMap[runtimeIndex]);
        add_edges_from_runtime_inputs(
            runtimeGraph->ctrlInputsOf(runtimeIndex),
            taskMap[runtimeIndex]);
    }

    if (const auto exitIndex = runtimeGraph->exitNodeRef();
        exitIndex != camel::runtime::kInvalidNodeRef) {
        if (exitIndex < taskBuilt.size() && taskBuilt[exitIndex] != 0) {
            for (auto inputIndex : runtimeGraph->normInputsOf(exitIndex)) {
                if (inputIndex < taskBuilt.size() && taskBuilt[inputIndex] != 0) {
                    taskMap[inputIndex].precede(taskMap[exitIndex]);
                }
            }
        }
    }
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildDataTask(FlowT &flowLike, Node *n, Frame *frame) {
    (void)n;
    (void)frame;
    return flowLike.emplace([]() {}).name("DATA");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildPortTask(FlowT &flowLike, Node *n, Frame *frame) {
    (void)n;
    (void)frame;
    return flowLike.emplace([]() {}).name("PORT");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCopyTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame]() {
            EXEC_WHEN_DEBUG({
                CAMEL_LOG_DEBUG_S(
                    "Taskflow",
                    "Executing COPY graph={}: {}",
                    n->graph().name(),
                    n->toString());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });
            auto inputNode    = n->normInputs().front();
            data_idx_t srcIdx = inputNode->index();
            TypeCode srcCode  = frame->codeAt(srcIdx);
            if (isGCTraced(srcCode)) {
                Object *srcData  = frame->get<Object *>(srcIdx);
                Type *srcTypePtr = frame->typeAt<Type>(srcIdx);
                frame->set(n->index(), srcData->clone(mm::autoSpace(), srcTypePtr, false));
            } else {
                frame->set(n->index(), frame->get<slot_t>(srcIdx));
            }
        })
        .name("COPY");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCastTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame]() {
            EXEC_WHEN_DEBUG({
                CAMEL_LOG_DEBUG_S(
                    "Taskflow",
                    "Executing CAST graph={}: {}",
                    n->graph().name(),
                    n->toString());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });
            auto inputNode = n->normInputs().front();
            Type *srcType  = frame->typeAt<Type>(inputNode->index());
            Type *tgtType  = n->dataType();
            slot_t value   = frame->get<slot_t>(inputNode->index());
            slot_t result  = tgtType->castSlotFrom(value, srcType);
            frame->set(n->index(), result);
        })
        .name("CAST");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFillTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame]() {
            EXEC_WHEN_DEBUG({
                CAMEL_LOG_DEBUG_S(
                    "Taskflow",
                    "Executing FILL graph={}: {}",
                    n->graph().name(),
                    n->toString());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });
            auto srcNode           = n->normInputs().front();
            const auto &dataInputs = n->withInputs();
            TypeCode srcCode       = frame->codeAt(srcNode->index());
            Type *srcType          = frame->typeAt<Type>(srcNode->index());
            ASSERT(isGCTraced(srcCode), "FILL target type is not GC-traced in Taskflow.");
            Object *srcObj =
                frame->get<Object *>(srcNode->index())->clone(mm::autoSpace(), srcType, false);
            ASSERT(srcObj != nullptr, "FILL target data is null.");
            switch (srcCode) {
            case TypeCode::Tuple: {
                auto type          = tt::as_ptr<TupleType>(srcType);
                auto tup           = tt::as_ptr<Tuple>(srcObj);
                const size_t *refs = type->refs();
                for (size_t j = 0; j < dataInputs.size(); ++j)
                    tup->set<slot_t>(refs[j], frame->get<slot_t>(dataInputs[j]->index()));
            } break;
            case TypeCode::Array: {
                auto arr = tt::as_ptr<Array>(srcObj);
                for (size_t j = 0; j < dataInputs.size(); ++j)
                    arr->set<slot_t>(j, frame->get<slot_t>(dataInputs[j]->index()));
            } break;
            case TypeCode::Struct: {
                auto type          = tt::as_ptr<StructType>(srcType);
                auto str           = tt::as_ptr<Struct>(srcObj);
                const size_t *refs = type->refs();
                for (size_t j = 0; j < dataInputs.size(); ++j)
                    str->set<slot_t>(refs[j], frame->get<slot_t>(dataInputs[j]->index()));
            } break;
            case TypeCode::Function: {
                auto func          = tt::as_ptr<Function>(srcObj);
                Tuple *closureData = func->tuple();
                for (size_t j = 0; j < dataInputs.size(); ++j)
                    closureData->set<slot_t>(j, frame->get<slot_t>(dataInputs[j]->index()));
            } break;
            default:
                ASSERT(
                    false,
                    std::format(
                        "Unsupported FILL target type {} in Taskflow.",
                        typeCodeToString(srcCode)));
            }
            frame->set(n->index(), srcObj);
        })
        .name("FILL");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildAccsTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame]() {
            EXEC_WHEN_DEBUG({
                CAMEL_LOG_DEBUG_S(
                    "Taskflow",
                    "Executing ACCS graph={}: {}",
                    n->graph().name(),
                    n->toString());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });
            auto accsNode     = tt::as_ptr<AccsNode>(n);
            data_idx_t srcIdx = n->dataInputs().front()->index();
            if (accsNode->isNum()) {
                size_t idx = accsNode->numIndex();
                Tuple *t   = frame->get<Tuple *>(srcIdx);
                ASSERT(idx < t->size(), "Tuple index out of bounds in Taskflow.");
                frame->set(n->index(), t->get<slot_t>(idx));
            } else {
                std::string key  = accsNode->strIndex();
                Struct *s        = frame->get<Struct *>(srcIdx);
                Type *structType = frame->typeAt<Type>(srcIdx);
                frame->set(n->index(), s->get<slot_t>(key, structType));
            }
        })
        .name("ACCS");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFuncTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
            EXEC_WHEN_DEBUG({
                CAMEL_LOG_DEBUG_S(
                    "Taskflow",
                    "Executing FUNC (entering subgraph) graph={} node={} -> subgraph={}",
                    n->graph().name(),
                    n->toString(),
                    tt::as_ptr<FuncNode>(n)->bodyGraph()->name());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });
            const auto &nodeMeta = taskflow_node_exec_meta(globalBuildCtx_, frame, n);
            auto *runtimeTarget =
                frame->runtimeGraph()
                    ? frame->runtimeGraph()->directCalleeGraphOf(nodeMeta.runtimeNodeIndex)
                    : nullptr;
            ASSERT(
                runtimeTarget != nullptr,
                "Taskflow direct FUNC target must have a materialized runtime graph.");
            Frame *funcFrame =
                acquirePreparedRuntimeCallFrame(runtimeTarget, nodeMeta.runtimeNodeIndex, frame);
            slot_t result = runPreparedSubgraph(sf, runtimeTarget, funcFrame);
            frame->set(n->index(), result);
        })
        .name("FUNC");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCallTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
            auto *runtimeTarget = frame->get<Function *>(n->withInputs().front()->index())->graph();
            ASSERT(
                runtimeTarget != nullptr,
                "Taskflow indirect CALL requires a materialized runtime graph target.");
            const auto &nodeMeta = taskflow_node_exec_meta(globalBuildCtx_, frame, n);
            EXEC_WHEN_DEBUG({
                CAMEL_LOG_DEBUG_S(
                    "Taskflow",
                    "Executing CALL graph={} node={} -> subgraph={}",
                    n->graph().name(),
                    n->toString(),
                    runtimeTarget->name());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });
            Frame *funcFrame =
                acquirePreparedRuntimeCallFrame(runtimeTarget, nodeMeta.runtimeNodeIndex, frame);
            frame->set(n->index(), runPreparedSubgraph(sf, runtimeTarget, funcFrame));
        })
        .name("CALL");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildOperTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
            EXEC_WHEN_DEBUG({
                CAMEL_LOG_DEBUG_S(
                    "Taskflow",
                    "Executing OPER graph={}: {} uri={}",
                    n->graph().name(),
                    n->toString(),
                    tt::as_ptr<OperNode>(n)->oper()->uri());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });
            auto opNode     = tt::as_ptr<OperNode>(n);
            const auto &uri = opNode->oper()->uri();
            if (uri.starts_with(":mark/")) {
                if (uri == ":mark/map_arr") {
                    mark_map_arr(n, frame, sf);
                } else if (uri == ":mark/apply_arr") {
                    mark_apply_arr(n, frame, sf);
                } else if (uri == ":mark/filter_arr") {
                    mark_filter_arr(n, frame, sf);
                } else if (uri == ":mark/reduce_arr") {
                    mark_reduce_arr(n, frame, sf);
                } else if (uri == ":mark/foreach_arr") {
                    mark_foreach_arr(n, frame, sf);
                } else if (uri == ":mark/unordered_foreach_arr") {
                    mark_unordered_foreach_arr(n, frame, sf);
                } else if (uri == ":mark/unordered_reduce_arr") {
                    mark_unordered_reduce_arr(n, frame, sf);
                } else {
                    ASSERT(false, std::format("Mark Operator {} not implemented.", uri.substr(6)));
                }
            } else {
                frame->set(n->index(), executePreparedOperator(n, frame));
            }
        })
        .name("OPER");
}

template <typename FlowT>
void TaskflowExecSchedPass::buildBranchJoinRegion(
    FlowT &flowLike, Frame *frame, std::unordered_map<Node *, tf::Task> &taskMap, Node *brch) {
    auto *brchNode         = tt::as_ptr<BrchNode>(brch);
    Graph *ownerGraph      = &brch->graph();
    Node *join             = brchNode->matchedJoin();
    const auto &branchArms = taskflow_branch_arms(globalBuildCtx_, frame, brch);

    auto selector =
        flowLike
            .emplace([brch, frame, this]() {
                const auto &nodeMeta = taskflow_node_exec_meta(globalBuildCtx_, frame, brch);
                const auto &normIns  = nodeMeta.normIndices;
                const auto &withIns  = nodeMeta.withIndices;
                ASSERT(normIns.size() == 1, "Branch node must have exactly one norm input.");
                size_t jumpIdx = 0;
                if (withIns.empty()) {
                    bool cond = frame->get<bool>(normIns.front());
                    jumpIdx   = cond ? 0 : 1;
                } else {
                    TypeCode condType = frame->codeAt(normIns.front());
                    size_t j          = 0;
                    if (isGCTraced(condType)) {
                        Type *condTypePtr = frame->typeAt<Type>(normIns.front());
                        Object *condData  = frame->get<Object *>(normIns.front());
                        for (; j < withIns.size(); ++j) {
                            Object *caseData = frame->get<Object *>(withIns[j]);
                            if (condData->equals(caseData, condTypePtr, false)) {
                                jumpIdx = j;
                                break;
                            }
                        }
                    } else {
                        slot_t condData = frame->get<slot_t>(normIns.front());
                        for (; j < withIns.size(); ++j) {
                            if (condData == frame->get<slot_t>(withIns[j])) {
                                jumpIdx = j;
                                break;
                            }
                        }
                    }
                    if (j == withIns.size())
                        jumpIdx = withIns.size();
                }
                frame->set(brch->index(), static_cast<Int32>(jumpIdx));
                EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                    "Taskflow",
                    "BRCH_SEL graph={} brch={} selected branch index {}",
                    brch->graph().name(),
                    brch->toString(),
                    jumpIdx));
            })
            .name("BRCH_SEL");

    auto joiner = flowLike
                      .emplace([join, frame]() {
                          (void)frame->get<slot_t>(join->index());
                          EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                              "Taskflow",
                              "JOIN graph={}: {}",
                              join->graph().name(),
                              join->toString()));
                      })
                      .name("JOIN");

    auto precede_from_inputs = [&](node_span_t inputs, tf::Task tsk) {
        for (const auto &in : inputs) {
            if (&in->graph() != ownerGraph)
                continue;
            if (taskflow_should_skip_node(globalBuildCtx_, frame, in) &&
                in->type() != NodeType::JOIN)
                continue;
            auto it = taskMap.find(in);
            if (it != taskMap.end())
                it->second.precede(tsk);
        }
    };

    precede_from_inputs(brch->dataInputs(), selector);
    precede_from_inputs(brch->ctrlInputs(), selector);

    for (size_t i = 0; i < branchArms.size(); ++i) {
        auto layer = flowLike.emplace([]() {}).name("BRCH_CAND_LAYER");

        tf::Task task_do =
            flowLike
                .emplace([i, brchNode, join, frame, this](tf::Subflow &csf) {
                    int32_t tarIdx = frame->get<int32_t>(brchNode->index());
                    if (static_cast<size_t>(tarIdx) != i) {
                        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                            "Taskflow",
                            "BRCH_CAND_EXEC graph={} branch [{}] skipped (selected={})",
                            brchNode->graph().name(),
                            i,
                            tarIdx));
                        return;
                    }
                    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                        "Taskflow",
                        "BRCH_CAND_EXEC graph={} executing branch [{}]",
                        brchNode->graph().name(),
                        i));

                    slot_t out = executePreparedBranchArm(brchNode, i, frame, csf);
                    frame->set(join->index(), out);
                })
                .name("BRCH_CAND_EXEC");

        selector.precede(layer);
        layer.precede(task_do);
        task_do.precede(joiner);
    }

    taskMap[brch] = selector;
    taskMap[join] = joiner;
}

template <typename FlowT>
void TaskflowExecSchedPass::buildRuntimeBranchJoinRegion(
    FlowT &flowLike, camel::runtime::GCGraph *runtimeGraph, Frame *frame,
    std::vector<tf::Task> &taskMap, std::vector<uint8_t> &taskBuilt,
    camel::runtime::gc_node_ref_t brchRuntimeIndex, Node *brch) {
    ASSERT(
        runtimeGraph != nullptr,
        "Taskflow runtime branch construction requires a runtime graph.");
    ASSERT(brch != nullptr, "Taskflow runtime branch construction requires source metadata.");
    auto *brchNode = tt::as_ptr<BrchNode>(brch);
    ASSERT(brchNode != nullptr, "Taskflow runtime branch construction requires a BRCH node.");
    ASSERT(brchRuntimeIndex < taskMap.size(), "Taskflow runtime BRCH index is out of range.");

    const auto &branchArms = taskflow_branch_arms(globalBuildCtx_, frame, brch);
    const auto &graphInfo  = globalBuildCtx_.getGraphInfos(runtimeGraph);
    ASSERT(
        graphInfo.getNodeExecMeta(brchRuntimeIndex).runtimeNodeIndex == brchRuntimeIndex,
        "Taskflow runtime BRCH metadata is inconsistent.");

    Node *join = brchNode->matchedJoin();
    ASSERT(join != nullptr, "Taskflow runtime BRCH requires a matched JOIN.");
    const auto &sourceJoinMeta =
        globalBuildCtx_.getGraphInfos(&brch->graph()).getNodeExecMeta(join);
    ASSERT(
        sourceJoinMeta.runtimeNodeIndex != camel::runtime::kInvalidNodeRef,
        "Taskflow runtime JOIN index is missing.");
    const uint32_t joinRuntimeIndex = sourceJoinMeta.runtimeNodeIndex;
    ASSERT(joinRuntimeIndex < taskMap.size(), "Taskflow runtime JOIN index is out of range.");

    auto selector =
        flowLike
            .emplace([brch, frame, this]() {
                const auto &nodeMeta = taskflow_node_exec_meta(globalBuildCtx_, frame, brch);
                const auto &normIns  = nodeMeta.normIndices;
                const auto &withIns  = nodeMeta.withIndices;
                ASSERT(normIns.size() == 1, "Branch node must have exactly one norm input.");
                size_t jumpIdx = 0;
                if (withIns.empty()) {
                    bool cond = frame->get<bool>(normIns.front());
                    jumpIdx   = cond ? 0 : 1;
                } else {
                    TypeCode condType = frame->codeAt(normIns.front());
                    size_t j          = 0;
                    if (isGCTraced(condType)) {
                        Type *condTypePtr = frame->typeAt<Type>(normIns.front());
                        Object *condData  = frame->get<Object *>(normIns.front());
                        for (; j < withIns.size(); ++j) {
                            Object *caseData = frame->get<Object *>(withIns[j]);
                            if (condData->equals(caseData, condTypePtr, false)) {
                                jumpIdx = j;
                                break;
                            }
                        }
                    } else {
                        slot_t condData = frame->get<slot_t>(normIns.front());
                        for (; j < withIns.size(); ++j) {
                            if (condData == frame->get<slot_t>(withIns[j])) {
                                jumpIdx = j;
                                break;
                            }
                        }
                    }
                    if (j == withIns.size()) {
                        jumpIdx = withIns.size();
                    }
                }
                frame->set(brch->index(), static_cast<Int32>(jumpIdx));
                EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                    "Taskflow",
                    "BRCH_SEL graph={} brch={} selected branch index {}",
                    brch->graph().name(),
                    brch->toString(),
                    jumpIdx));
            })
            .name("BRCH_SEL");

    auto joiner = flowLike
                      .emplace([join, frame]() {
                          (void)frame->get<slot_t>(join->index());
                          EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                              "Taskflow",
                              "JOIN graph={}: {}",
                              join->graph().name(),
                              join->toString()));
                      })
                      .name("JOIN");

    auto precede_from_runtime_inputs = [&](std::span<const camel::runtime::gc_node_ref_t> inputs,
                                           tf::Task tsk) {
        for (uint32_t inputIndex : inputs) {
            const auto *inputRecord = runtimeGraph->node(inputIndex);
            if (inputRecord == nullptr) {
                continue;
            }
            if (graphInfo.runtimeSkipNodeIndices.contains(inputIndex) &&
                inputRecord->kind != camel::runtime::GCNodeKind::Join) {
                continue;
            }
            if (inputIndex < taskBuilt.size() && taskBuilt[inputIndex] != 0) {
                taskMap[inputIndex].precede(tsk);
            }
        }
    };

    precede_from_runtime_inputs(runtimeGraph->normInputsOf(brchRuntimeIndex), selector);
    precede_from_runtime_inputs(runtimeGraph->ctrlInputsOf(brchRuntimeIndex), selector);

    for (size_t i = 0; i < branchArms.size(); ++i) {
        auto layer = flowLike.emplace([]() {}).name("BRCH_CAND_LAYER");

        tf::Task task_do =
            flowLike
                .emplace([i, brchNode, join, frame, this](tf::Subflow &csf) {
                    int32_t tarIdx = frame->get<int32_t>(brchNode->index());
                    if (static_cast<size_t>(tarIdx) != i) {
                        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                            "Taskflow",
                            "BRCH_CAND_EXEC graph={} branch [{}] skipped (selected={})",
                            brchNode->graph().name(),
                            i,
                            tarIdx));
                        return;
                    }
                    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                        "Taskflow",
                        "BRCH_CAND_EXEC graph={} executing branch [{}]",
                        brchNode->graph().name(),
                        i));

                    slot_t out = executePreparedBranchArm(brchNode, i, frame, csf);
                    frame->set(join->index(), out);
                })
                .name("BRCH_CAND_EXEC");

        selector.precede(layer);
        layer.precede(task_do);
        task_do.precede(joiner);
    }

    taskMap[brchRuntimeIndex]   = selector;
    taskBuilt[brchRuntimeIndex] = 1;
    taskMap[joinRuntimeIndex]   = joiner;
    taskBuilt[joinRuntimeIndex] = 1;
}

template <typename FlowT>
void TaskflowExecSchedPass::buildNormalNodeTasks(
    FlowT &flowLike, Graph *graph, Frame *frame, std::unordered_map<Node *, tf::Task> &taskMap) {
    for (Node *n : collect_taskflow_execution_nodes(graph)) {
        if (taskflow_should_skip_node(globalBuildCtx_, frame, n)) {
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "Taskflow",
                "Skipping node (BRCH region) graph={}: {}",
                graph->name(),
                n->toString()));
            continue;
        }
        tf::Task t;
        switch (n->type()) {
        case NodeType::DATA:
            t = buildDataTask(flowLike, n, frame);
            break;
        case NodeType::PORT:
            t = buildPortTask(flowLike, n, frame);
            break;
        case NodeType::COPY:
            t = buildCopyTask(flowLike, n, frame);
            break;
        case NodeType::CAST:
            t = buildCastTask(flowLike, n, frame);
            break;
        case NodeType::FILL:
            t = buildFillTask(flowLike, n, frame);
            break;
        case NodeType::ACCS:
            t = buildAccsTask(flowLike, n, frame);
            break;
        case NodeType::FUNC:
            t = buildFuncTask(flowLike, n, frame);
            break;
        case NodeType::CALL:
            t = buildCallTask(flowLike, n, frame);
            break;
        case NodeType::OPER:
            t = buildOperTask(flowLike, n, frame);
            break;
        case NodeType::SYNC:
            [[fallthrough]];
        case NodeType::GATE:
            // Match NodeVM semantics: GATE has no executable side effects and only forwards the
            // value already defined by GIR connectivity. Taskflow still needs a task node here so
            // dependency edges remain ordered.
            if (n->type() == NodeType::GATE) {
                t = flowLike
                        .emplace([n, frame]() {
                            auto dataInputs = n->dataInputs();
                            if (!dataInputs.empty()) {
                                slot_t value = read_taskflow_node_value(dataInputs.back(), frame);
                                if (n->index() != 0) {
                                    frame->set(n->index(), value);
                                }
                            }
                        })
                        .name("GATE");
            } else {
                t = flowLike.emplace([]() {}).name("SYNC");
            }
            break;
        case NodeType::BRCH:
            buildBranchJoinRegion(flowLike, frame, taskMap, n);
            continue;
        default:
            ASSERT(false, std::format("Unsupported node type: {}", n->toString()));
        }
        taskMap[n] = t;
    }

    for (const auto &port : graph->ports()) {
        tf::Task t    = buildPortTask(flowLike, port, frame);
        taskMap[port] = t;
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::connectDependencies(
    FlowT &flow, Graph *graph, Frame *frame, std::unordered_map<Node *, tf::Task> &taskMap) {
    auto add_edges_from_inputs = [&](node_span_t inputs, tf::Task tsk) {
        for (const auto &in : inputs) {
            if (&in->graph() != graph)
                continue;
            if (taskflow_should_skip_node(globalBuildCtx_, frame, in) &&
                in->type() != NodeType::JOIN)
                continue;
            auto it = taskMap.find(in);
            if (it != taskMap.end())
                it->second.precede(tsk);
        }
    };

    for (Node *n : collect_taskflow_execution_nodes(graph)) {
        if (n->type() == NodeType::BRCH)
            continue;
        if (taskflow_should_skip_node(globalBuildCtx_, frame, n))
            continue;
        auto it = taskMap.find(n);
        if (it == taskMap.end())
            continue;
        add_edges_from_inputs(n->dataInputs(), it->second);
        add_edges_from_inputs(n->ctrlInputs(), it->second);
    }

    Node *exitNode = graph->exitNode();
    auto it        = taskMap.find(exitNode);
    if (it != taskMap.end()) {
        add_edges_from_inputs(exitNode->dataInputs(), it->second);
        add_edges_from_inputs(exitNode->ctrlInputs(), it->second);
    }
}

void TaskflowExecSchedPass::mark_map_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr      = frame->get<Array *>(node->normInputs().front()->index());
    Function *func  = frame->get<Function *>(node->withInputs().front()->index());
    const auto site = make_taskflow_higher_order_call_site(func);
    const size_t n  = arr->size();

    std::vector<slot_t> results(n);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, site, &results](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *f   = acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
              results[i] = runPreparedSubgraph(isf, site.runtimeGraph, f);
          }).name("MAP_ELEM");
    }
    sf.join();
    Array *res = Array::create(mm::autoSpace(), n);
    for (size_t i = 0; i < n; ++i)
        res->set(i, results[i]);
    frame->set(node->index(), res);
}

void TaskflowExecSchedPass::mark_apply_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr      = frame->get<Array *>(node->normInputs().front()->index());
    Function *func  = frame->get<Function *>(node->withInputs().front()->index());
    const auto site = make_taskflow_higher_order_call_site(func);
    const size_t n  = arr->size();

    std::vector<slot_t> results(n);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, site, &results](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *f   = acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
              results[i] = runPreparedSubgraph(isf, site.runtimeGraph, f);
          }).name("APPLY_ELEM");
    }
    sf.join();
    for (size_t i = 0; i < n; ++i)
        arr->set(i, results[i]);
    frame->set(node->index(), arr);
}

void TaskflowExecSchedPass::mark_filter_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr      = frame->get<Array *>(node->normInputs().front()->index());
    Function *func  = frame->get<Function *>(node->withInputs().front()->index());
    const auto site = make_taskflow_higher_order_call_site(func);
    const size_t n  = arr->size();

    std::vector<bool> keep(n, false);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, site, &keep](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *f = acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
              keep[i]  = fromSlot<bool>(runPreparedSubgraph(isf, site.runtimeGraph, f));
          }).name("FILTER_PRED");
    }
    sf.join();
    Array *filtered = Array::create(mm::autoSpace(), 0);
    for (size_t i = 0; i < n; ++i)
        if (keep[i])
            filtered->append(arr->get<slot_t>(i));
    filtered->shrinkToFit();
    frame->set(node->index(), filtered);
}

void TaskflowExecSchedPass::mark_reduce_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr      = frame->get<Array *>(node->normInputs().front()->index());
    Function *func  = frame->get<Function *>(node->withInputs()[0]->index());
    slot_t init     = frame->get<slot_t>(node->withInputs()[1]->index());
    const auto site = make_taskflow_higher_order_call_site(func);

    if (arr->size() == 0) {
        frame->set(node->index(), init);
        return;
    }

    auto accPtr = std::make_shared<slot_t>(init);
    tf::Task prev;
    bool has_prev = false;
    for (size_t i = 0; i < arr->size(); ++i) {
        slot_t elem = arr->get<slot_t>(i);
        tf::Task step =
            sf.emplace([this, accPtr, elem, site](tf::Subflow &isf) {
                  std::array<slot_t, 2> args{*accPtr, elem};
                  Frame *f = acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
                  *accPtr  = runPreparedSubgraph(isf, site.runtimeGraph, f);
              }).name("REDUCE_STEP");
        if (has_prev)
            step.succeed(prev);
        prev     = step;
        has_prev = true;
    }
    sf.join();
    frame->set(node->index(), *accPtr);
}

void TaskflowExecSchedPass::mark_unordered_reduce_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    // Keep the same semantics as reduce_arr for now. The implementation is a sequential left fold
    // and can be upgraded to parallel divide-and-conquer later.
    mark_reduce_arr(node, frame, sf);
}

void TaskflowExecSchedPass::mark_foreach_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr      = frame->get<Array *>(node->normInputs().front()->index());
    Function *func  = frame->get<Function *>(node->withInputs().front()->index());
    const auto site = make_taskflow_higher_order_call_site(func);

    tf::Task prev;
    bool has_prev = false;
    for (size_t i = 0; i < arr->size(); ++i) {
        slot_t elem = arr->get<slot_t>(i);
        tf::Task step =
            sf.emplace([this, elem, site](tf::Subflow &isf) {
                  std::array<slot_t, 1> args{elem};
                  Frame *f = acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
                  (void)runPreparedSubgraph(isf, site.runtimeGraph, f);
              }).name("FOREACH_ELEM");
        if (has_prev)
            step.succeed(prev);
        prev     = step;
        has_prev = true;
    }
    sf.join();
    frame->set(node->index(), NullSlot);
}

void TaskflowExecSchedPass::mark_unordered_foreach_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr      = frame->get<Array *>(node->normInputs().front()->index());
    Function *func  = frame->get<Function *>(node->withInputs().front()->index());
    const auto site = make_taskflow_higher_order_call_site(func);
    const size_t n  = arr->size();

    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, site](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *f = acquirePreparedClosureCallFrame(site.runtimeGraph, site.closure, args);
              (void)runPreparedSubgraph(isf, site.runtimeGraph, f);
          }).name("FOREACH_ELEM");
    }
    sf.join();
    frame->set(node->index(), NullSlot);
}

template void TaskflowExecSchedPass::instantiate_graph_instance_generic<tf::Taskflow>(
    tf::Taskflow &flowLike, GIR::Graph *graph, ctx::Frame *frame);
