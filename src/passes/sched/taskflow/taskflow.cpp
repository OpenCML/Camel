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
 * Updated: Mar. 15, 2026
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
#include "camel/utils/debug.h"
#include "camel/utils/log.h"

#include <array>
#include <queue>
#include <regex>
#include <unordered_set>

using namespace std;
using namespace GIR;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

// 从图的 exit 节点读取返回值（与 NodeVM 一致：slot_t）
static slot_t get_graph_return(Graph *g, Frame *frame) {
    auto retNode = g->exitNode();
    if (retNode->normInputs().empty())
        return NullSlot;
    return frame->get<slot_t>(retNode->normInputs().front()->index());
}

graph_ptr_t TaskflowExecSchedPass::apply(graph_ptr_t &graph, std::ostream & /*os*/) {
    if (!graph->hasOutput()) {
        throw reportRuntimeFault(
            *context_,
            RuntimeFault::make(RuntimeDiag::MissingMainFunction, context_->mainModule()->name()),
            makeGraphExecutionSite(context_->sourceContext(), graph.get(), 0, "taskflow"));
    }

    buildGraphsInfo(graph.get());

    Frame *rootFrame = framePool_.acquire(graph.get());
    try {
        slot_t result = evalGraphTF(graph.get(), rootFrame);
        context_->captureProcessExitCode(graph.get(), result);
        framePool_.release(rootFrame);
    } catch (...) {
        framePool_.release(rootFrame);
        throw;
    }

    return Graph::null();
}

slot_t TaskflowExecSchedPass::evalGraphTF(Graph *graph, Frame *frame) {
    EXEC_WHEN_DEBUG(GetDefaultLogger().in("Taskflow").debug("Evaluating graph: {}", graph->name()));
    mainFlow_.clear();
    instantiate_graph_instance_generic(mainFlow_, graph, frame);
    executor_.run(mainFlow_).wait();
    slot_t ret = get_graph_return(graph, frame);
    EXEC_WHEN_DEBUG(GetDefaultLogger().in("Taskflow").debug("Graph {} finished.", graph->name()));
    return ret;
}

slot_t TaskflowExecSchedPass::runPreparedSubgraph(tf::Subflow &sf, Graph *graph, Frame *frame) {
    try {
        instantiate_graph_instance_generic(sf, graph, frame);
        sf.join();
        slot_t result = get_graph_return(graph, frame);
        framePool_.release(frame);
        return result;
    } catch (...) {
        framePool_.release(frame);
        throw;
    }
}

Frame *TaskflowExecSchedPass::acquirePreparedNodeCallFrame(
    Graph *targetGraph, Node *callNode, Frame *sourceFrame) {
    Frame *dest            = framePool_.acquire(targetGraph);
    const auto &targetInfo = globalBuildCtx_.getGraphInfos(targetGraph);
    const auto &callMeta =
        globalBuildCtx_.getGraphInfos(&callNode->graph()).getNodeExecMeta(callNode);
    const auto &normPortIndices = targetInfo.normPortIndices;
    const auto &withPortIndices = targetInfo.withPortIndices;
    const auto &closureIndices  = targetInfo.closureIndices;
    const auto &normIndices     = callMeta.normIndices;
    const auto &withIndices     = callMeta.withIndices;

    ASSERT(
        normIndices.size() == normPortIndices.size(),
        "Norm nodes and ports count mismatch in Taskflow call frame fill.");
    for (size_t i = 0; i < normIndices.size(); ++i)
        dest->set(normPortIndices[i], sourceFrame->get<slot_t>(normIndices[i]));

    if (callNode->type() == NodeType::CALL) {
        ASSERT(!withIndices.empty(), "CALL node must have function input.");
        ASSERT(
            withPortIndices.empty(),
            "CALL target graph should not require with ports in Taskflow.");
        Function *func = sourceFrame->get<Function *>(withIndices.front());
        Tuple *closure = func->tuple();
        ASSERT(
            closure->size() == closureIndices.size(),
            "Closure nodes and tuple size mismatch in Taskflow call frame fill.");
        for (size_t j = 0; j < closure->size(); ++j)
            dest->set(closureIndices[j], closure->get<slot_t>(j));
    } else {
        ASSERT(
            withIndices.size() == withPortIndices.size(),
            "With nodes and ports count mismatch in Taskflow func frame fill.");
        for (size_t j = 0; j < withIndices.size(); ++j)
            dest->set(withPortIndices[j], sourceFrame->get<slot_t>(withIndices[j]));
    }

    return dest;
}

Frame *TaskflowExecSchedPass::acquirePreparedClosureCallFrame(
    Graph *targetGraph, Tuple *closure, std::span<const slot_t> args) {
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
                &n->graph(),
                n,
                0,
                "taskflow",
                ExecutionSiteKind::TaskNode));
    }

    auto &meta = globalBuildCtx_.getGraphInfos(&n->graph()).getOrCreateNodeExecMeta(n);
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
                &n->graph(),
                n,
                0,
                "taskflow",
                ExecutionSiteKind::TaskNode));
    }
}

void TaskflowExecSchedPass::buildGraphsInfo(Graph *rootGraph) {
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

        auto &gt = globalBuildCtx_.getOrCreateGraphInfos(g);
        gt.graph = g;
        gt.joinToBrch.clear();
        gt.nodeExecMeta.clear();
        gt.normPortIndices.clear();
        gt.withPortIndices.clear();
        gt.closureIndices.clear();

        gt.normPortIndices.reserve(g->normPorts().size());
        for (Node *port : g->normPorts())
            gt.normPortIndices.push_back(port->index());

        gt.withPortIndices.reserve(g->withPorts().size());
        for (Node *port : g->withPorts())
            gt.withPortIndices.push_back(port->index());

        gt.closureIndices.reserve(g->closure().size());
        for (Node *closure : g->closure())
            gt.closureIndices.push_back(closure->index());

        for (Node *n : g->nodes()) {
            auto &nodeMeta = gt.getOrCreateNodeExecMeta(n);
            nodeMeta.normIndices.clear();
            nodeMeta.withIndices.clear();
            nodeMeta.normIndices.reserve(n->normInputs().size());
            nodeMeta.withIndices.reserve(n->withInputs().size());
            for (Node *in : n->normInputs())
                nodeMeta.normIndices.push_back(in->index());
            for (Node *in : n->withInputs())
                nodeMeta.withIndices.push_back(in->index());

            if (n->type() == NodeType::FUNC) {
                auto fn    = tt::as_ptr<FuncNode>(n);
                Graph *sub = &fn->func()->graph();
                if (!visited.count(sub))
                    q.push(sub);
            } else if (n->type() == NodeType::BRCH) {
                const auto candidates = n->ctrlOutputs();
                Node *join            = n->normOutputs().front();
                for (const auto &c : candidates)
                    globalBuildCtx_.skipNodes.insert(c);
                globalBuildCtx_.skipNodes.insert(n);
                globalBuildCtx_.skipNodes.insert(join);
                ASSERT(join->type() == NodeType::JOIN, "BRCH must be paired with JOIN.");
                gt.joinToBrch[join] = n;
            }
        }

        for (const auto &[_, graphsSet] : g->subGraphs()) {
            for (const auto &sg : graphsSet) {
                if (!visited.count(sg.get()))
                    q.push(sg.get());
            }
        }

        // 提前安装 FrameMeta 并预热一个 frame，避免首次进入子图时在 worker 线程里做冷启动。
        framePool_.warmup(g, 1);
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::instantiate_graph_instance_generic(
    FlowT &flowLike, Graph *graph, Frame *frame) {
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("Taskflow")
            .debug(
                "Instantiating graph instance: {} (nodes={})",
                graph->name(),
                graph->nodes().size()));
    std::unordered_map<Node *, tf::Task> taskMap;
    buildNormalNodeTasks(flowLike, graph, frame, taskMap);
    connectDependencies(flowLike, graph, taskMap);
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildExitTask(FlowT &flowLike, Node *n, Frame *frame) {
    (void)n;
    (void)frame;
    return flowLike.emplace([]() {}).name("EXIT");
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
                GetDefaultLogger()
                    .in("Taskflow")
                    .debug("Executing COPY graph={}: {}", n->graph().name(), n->toString());
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
                GetDefaultLogger()
                    .in("Taskflow")
                    .debug("Executing CAST graph={}: {}", n->graph().name(), n->toString());
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
                GetDefaultLogger()
                    .in("Taskflow")
                    .debug("Executing FILL graph={}: {}", n->graph().name(), n->toString());
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
                GetDefaultLogger()
                    .in("Taskflow")
                    .debug("Executing ACCS graph={}: {}", n->graph().name(), n->toString());
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
                GetDefaultLogger()
                    .in("Taskflow")
                    .debug(
                        "Executing FUNC (entering subgraph) graph={} node={} -> subgraph={}",
                        n->graph().name(),
                        n->toString(),
                        tt::as_ptr<FuncNode>(n)->func()->graph().name());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });
            Graph *tgtGraph  = &tt::as_ptr<FuncNode>(n)->func()->graph();
            Frame *funcFrame = acquirePreparedNodeCallFrame(tgtGraph, n, frame);
            frame->set(n->index(), runPreparedSubgraph(sf, tgtGraph, funcFrame));
        })
        .name("FUNC");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCallTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
            Graph *tgtGraph = frame->get<Function *>(n->withInputs().front()->index())->graph();
            EXEC_WHEN_DEBUG({
                GetDefaultLogger()
                    .in("Taskflow")
                    .debug(
                        "Executing CALL graph={} node={} -> subgraph={}",
                        n->graph().name(),
                        n->toString(),
                        tgtGraph->name());
                if (camel::DebugBreakpoint::IsEnabled("gir_node"))
                    camel::DebugBreakpoint::Hit("gir_node", n);
            });
            Frame *funcFrame = acquirePreparedNodeCallFrame(tgtGraph, n, frame);
            frame->set(n->index(), runPreparedSubgraph(sf, tgtGraph, funcFrame));
        })
        .name("CALL");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildOperTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
            EXEC_WHEN_DEBUG({
                GetDefaultLogger()
                    .in("Taskflow")
                    .debug(
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
    FlowT &flowLike, Graph *graph, Frame *frame, std::unordered_map<Node *, tf::Task> &taskMap,
    Node *brch) {
    auto *brchNode = tt::as_ptr<BrchNode>(brch);
    auto ctrlOuts  = brch->ctrlOutputs();
    node_vec_t candidates(ctrlOuts.begin(), ctrlOuts.end());
    Node *join = brchNode->matchedJoin();

    auto selector =
        flowLike
            .emplace([brch, frame]() {
                const auto &normIns = brch->normInputs();
                const auto &withIns = brch->withInputs();
                ASSERT(normIns.size() == 1, "Branch node must have exactly one norm input.");
                size_t jumpIdx = 0;
                if (withIns.empty()) {
                    bool cond = frame->get<bool>(normIns.front()->index());
                    jumpIdx   = cond ? 0 : 1;
                } else {
                    TypeCode condType = frame->codeAt(normIns.front()->index());
                    size_t j          = 0;
                    if (isGCTraced(condType)) {
                        Type *condTypePtr = frame->typeAt<Type>(normIns.front()->index());
                        Object *condData  = frame->get<Object *>(normIns.front()->index());
                        for (; j < withIns.size(); ++j) {
                            Object *caseData = frame->get<Object *>(withIns[j]->index());
                            if (condData->equals(caseData, condTypePtr, false)) {
                                jumpIdx = j;
                                break;
                            }
                        }
                    } else {
                        slot_t condData = frame->get<slot_t>(normIns.front()->index());
                        for (; j < withIns.size(); ++j) {
                            if (condData == frame->get<slot_t>(withIns[j]->index())) {
                                jumpIdx = j;
                                break;
                            }
                        }
                    }
                    if (j == withIns.size())
                        jumpIdx = withIns.size();
                }
                frame->set(brch->index(), static_cast<Int32>(jumpIdx));
                EXEC_WHEN_DEBUG(
                    GetDefaultLogger()
                        .in("Taskflow")
                        .debug(
                            "BRCH_SEL graph={} brch={} selected branch index {}",
                            brch->graph().name(),
                            brch->toString(),
                            jumpIdx));
            })
            .name("BRCH_SEL");

    auto joiner =
        flowLike
            .emplace([join, frame]() {
                (void)frame->get<slot_t>(join->index());
                EXEC_WHEN_DEBUG(
                    GetDefaultLogger()
                        .in("Taskflow")
                        .debug("JOIN graph={}: {}", join->graph().name(), join->toString()));
            })
            .name("JOIN");

    auto precede_from_inputs = [&](node_span_t inputs, tf::Task tsk) {
        for (const auto &in : inputs) {
            if (&in->graph() != graph)
                continue;
            if (globalBuildCtx_.skipNodes.count(in) && in->type() != NodeType::JOIN)
                continue;
            auto it = taskMap.find(in);
            if (it != taskMap.end())
                it->second.precede(tsk);
        }
    };

    precede_from_inputs(brch->dataInputs(), selector);
    precede_from_inputs(brch->ctrlInputs(), selector);

    for (size_t i = 0; i < candidates.size(); ++i) {
        Node *candidate = candidates[i];
        auto layer      = flowLike.emplace([]() {}).name("BRCH_CAND_LAYER");

        tf::Task task_do =
            flowLike
                .emplace([i, candidate, brch, join, frame, this](tf::Subflow &csf) {
                    int32_t tarIdx = frame->get<int32_t>(brch->index());
                    if (static_cast<size_t>(tarIdx) != i) {
                        EXEC_WHEN_DEBUG(
                            GetDefaultLogger()
                                .in("Taskflow")
                                .debug(
                                    "BRCH_CAND_EXEC graph={} branch [{}] skipped (selected={})",
                                    candidate->graph().name(),
                                    i,
                                    tarIdx));
                        return;
                    }
                    EXEC_WHEN_DEBUG(
                        GetDefaultLogger()
                            .in("Taskflow")
                            .debug(
                                "BRCH_CAND_EXEC graph={} executing branch [{}] candidate={}",
                                candidate->graph().name(),
                                i,
                                candidate->toString()));

                    slot_t out = NullSlot;
                    if (candidate->type() == NodeType::FUNC) {
                        Graph *tgtGraph  = &tt::as_ptr<FuncNode>(candidate)->func()->graph();
                        Frame *funcFrame = acquirePreparedNodeCallFrame(tgtGraph, candidate, frame);
                        out              = runPreparedSubgraph(csf, tgtGraph, funcFrame);
                    } else if (candidate->type() == NodeType::CALL) {
                        auto *callNode = tt::as_ptr<CallNode>(candidate);
                        Graph *tgtGraph =
                            frame->get<Function *>(callNode->calleeInput()->index())->graph();
                        Frame *funcFrame = acquirePreparedNodeCallFrame(tgtGraph, candidate, frame);
                        out              = runPreparedSubgraph(csf, tgtGraph, funcFrame);
                    } else if (candidate->type() == NodeType::OPER) {
                        out = executePreparedOperator(candidate, frame);
                    } else {
                        ASSERT(false, "Unsupported candidate node type in BRCH-JOIN.");
                    }

                    frame->set(candidate->index(), out);
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
void TaskflowExecSchedPass::buildNormalNodeTasks(
    FlowT &flowLike, Graph *graph, Frame *frame, std::unordered_map<Node *, tf::Task> &taskMap) {
    std::unordered_set<Node *> &skipNodes = globalBuildCtx_.skipNodes;
    for (Node *n : graph->nodes()) {
        if (skipNodes.count(n) && n->type() != NodeType::BRCH) {
            EXEC_WHEN_DEBUG(
                GetDefaultLogger()
                    .in("Taskflow")
                    .debug(
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
        case NodeType::EXIT:
            t = buildExitTask(flowLike, n, frame);
            break;
        case NodeType::BRCH:
            buildBranchJoinRegion(flowLike, graph, frame, taskMap, n);
            continue;
        default:
            ASSERT(false, "Unsupported node type.");
        }
        taskMap[n] = t;
    }

    for (const auto &port : graph->ports()) {
        tf::Task t    = buildPortTask(flowLike, port, frame);
        taskMap[port] = t;
    }
    Node *exitNode    = graph->exitNode();
    tf::Task exitTask = buildExitTask(flowLike, exitNode, frame);
    taskMap[exitNode] = exitTask;
}

template <typename FlowT>
void TaskflowExecSchedPass::connectDependencies(
    FlowT &flow, Graph *graph, std::unordered_map<Node *, tf::Task> &taskMap) {
    std::unordered_set<Node *> &skipNodes = globalBuildCtx_.skipNodes;

    auto add_edges_from_inputs = [&](node_span_t inputs, tf::Task tsk) {
        for (const auto &in : inputs) {
            if (&in->graph() != graph)
                continue;
            if (skipNodes.count(in) && in->type() != NodeType::JOIN)
                continue;
            auto it = taskMap.find(in);
            if (it != taskMap.end())
                it->second.precede(tsk);
        }
    };

    for (Node *n : graph->nodes()) {
        if (n->type() == NodeType::BRCH)
            continue;
        if (skipNodes.count(n))
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
    Array *arr     = frame->get<Array *>(node->normInputs().front()->index());
    Function *func = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g       = func->graph();
    Tuple *closure = func->tuple();
    const size_t n = arr->size();

    std::vector<slot_t> results(n);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, g, closure, &results](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *f   = acquirePreparedClosureCallFrame(g, closure, args);
              results[i] = runPreparedSubgraph(isf, g, f);
          }).name("MAP_ELEM");
    }
    sf.join();
    Array *res = Array::create(mm::autoSpace(), n);
    for (size_t i = 0; i < n; ++i)
        res->set(i, results[i]);
    frame->set(node->index(), res);
}

void TaskflowExecSchedPass::mark_apply_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr     = frame->get<Array *>(node->normInputs().front()->index());
    Function *func = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g       = func->graph();
    Tuple *closure = func->tuple();
    const size_t n = arr->size();

    std::vector<slot_t> results(n);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, g, closure, &results](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *f   = acquirePreparedClosureCallFrame(g, closure, args);
              results[i] = runPreparedSubgraph(isf, g, f);
          }).name("APPLY_ELEM");
    }
    sf.join();
    for (size_t i = 0; i < n; ++i)
        arr->set(i, results[i]);
    frame->set(node->index(), arr);
}

void TaskflowExecSchedPass::mark_filter_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr     = frame->get<Array *>(node->normInputs().front()->index());
    Function *func = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g       = func->graph();
    Tuple *closure = func->tuple();
    const size_t n = arr->size();

    std::vector<bool> keep(n, false);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, g, closure, &keep](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *f = acquirePreparedClosureCallFrame(g, closure, args);
              keep[i]  = fromSlot<bool>(runPreparedSubgraph(isf, g, f));
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
    Array *arr     = frame->get<Array *>(node->normInputs().front()->index());
    Function *func = frame->get<Function *>(node->withInputs()[0]->index());
    slot_t init    = frame->get<slot_t>(node->withInputs()[1]->index());
    Graph *g       = func->graph();
    Tuple *closure = func->tuple();

    if (arr->size() == 0) {
        frame->set(node->index(), init);
        return;
    }

    auto accPtr = std::make_shared<slot_t>(init);
    tf::Task prev;
    bool has_prev = false;
    for (size_t i = 0; i < arr->size(); ++i) {
        slot_t elem   = arr->get<slot_t>(i);
        tf::Task step = sf.emplace([this, accPtr, elem, g, closure](tf::Subflow &isf) {
                              std::array<slot_t, 2> args{*accPtr, elem};
                              Frame *f = acquirePreparedClosureCallFrame(g, closure, args);
                              *accPtr  = runPreparedSubgraph(isf, g, f);
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
    // 与 reduce_arr 相同语义，当前用顺序左折叠实现；可后续改为并行 D&C
    mark_reduce_arr(node, frame, sf);
}

void TaskflowExecSchedPass::mark_foreach_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr     = frame->get<Array *>(node->normInputs().front()->index());
    Function *func = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g       = func->graph();
    Tuple *closure = func->tuple();

    tf::Task prev;
    bool has_prev = false;
    for (size_t i = 0; i < arr->size(); ++i) {
        slot_t elem   = arr->get<slot_t>(i);
        tf::Task step = sf.emplace([this, elem, g, closure](tf::Subflow &isf) {
                              std::array<slot_t, 1> args{elem};
                              Frame *f = acquirePreparedClosureCallFrame(g, closure, args);
                              (void)runPreparedSubgraph(isf, g, f);
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
    Array *arr     = frame->get<Array *>(node->normInputs().front()->index());
    Function *func = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g       = func->graph();
    Tuple *closure = func->tuple();
    const size_t n = arr->size();

    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, g, closure](tf::Subflow &isf) {
              std::array<slot_t, 1> args{arr->get<slot_t>(i)};
              Frame *f = acquirePreparedClosureCallFrame(g, closure, args);
              (void)runPreparedSubgraph(isf, g, f);
          }).name("FOREACH_ELEM");
    }
    sf.join();
    frame->set(node->index(), NullSlot);
}
