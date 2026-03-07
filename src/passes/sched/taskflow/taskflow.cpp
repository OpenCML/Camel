#include "taskflow.h"
#include "camel/common/algo/topo.h"
#include "camel/compile/gir/nodes.h"
#include "camel/core/mm.h"
#include "camel/core/module/module.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"

#include <queue>
#include <regex>
#include <unordered_set>

using namespace std;
using namespace GraphIR;

// 从图的 exit 节点读取返回值（与 NodeVM 一致：slot_t）
static slot_t get_graph_return(Graph *g, Frame *frame) {
    auto retNode = g->exitNode();
    if (retNode->normInputs().empty())
        return NullSlot;
    return frame->get<slot_t>(retNode->normInputs().front()->index());
}

// 将 CALL/FUNC 节点 n 的参数从 source 帧复制到 dest 帧（与 NodeVM fillFrameForCall 一致）
static void fillFrameForCall(Frame *dest, Graph *targetGraph, Node *n, Frame *source) {
    node_vec_t argNodes;
    const auto &closureNodes = targetGraph->closure();
    const auto &portNodes    = targetGraph->ports();

    if (n->type() == NodeType::CALL) {
        argNodes = n->normInputs();
        for (size_t i = 0; i < argNodes.size() && i < portNodes.size(); ++i)
            dest->set(portNodes[i]->index(), source->get<slot_t>(argNodes[i]->index()));
        Function *func = source->get<Function *>(n->withInputs().front()->index());
        Tuple *closure = func->tuple();
        for (size_t j = 0; j < closure->size() && j < closureNodes.size(); ++j)
            dest->set(closureNodes[j]->index(), closure->get<slot_t>(j));
    } else {
        argNodes = n->normInputs();
        for (size_t i = 0; i < argNodes.size() && i < portNodes.size(); ++i)
            dest->set(portNodes[i]->index(), source->get<slot_t>(argNodes[i]->index()));
        for (size_t j = 0; j < n->withInputs().size() && j < closureNodes.size(); ++j)
            dest->set(closureNodes[j]->index(), source->get<slot_t>(n->withInputs()[j]->index()));
    }
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
        evalGraphTF(graph.get(), rootFrame);
        framePool_.release(rootFrame);
    } catch (...) {
        framePool_.release(rootFrame);
        throw;
    }

    return Graph::null();
}

slot_t TaskflowExecSchedPass::evalGraphTF(Graph *graph, Frame *frame) {
    mainFlow_.clear();
    instantiate_graph_instance_generic(mainFlow_, graph, frame);
    executor_.run(mainFlow_).wait();
    return get_graph_return(graph, frame);
}

void TaskflowExecSchedPass::buildGraphsInfo(Graph *rootGraph) {
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

        for (Node *n : g->nodes()) {
            if (n->type() == NodeType::FUNC) {
                auto fn    = tt::as_ptr<FuncNode>(n);
                Graph *sub = &fn->func()->graph();
                if (!visited.count(sub))
                    q.push(sub);
            } else if (n->type() == NodeType::BRCH) {
                const node_vec_t &candidates = n->ctrlOutputs();
                Node *join                   = n->dataOutputs().front();
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
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::instantiate_graph_instance_generic(
    FlowT &flowLike, Graph *graph, Frame *frame) {
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
            auto accsNode     = tt::as_ptr<AccsNode>(n);
            data_idx_t srcIdx = n->dataInputs().front()->index();
            if (accsNode->isNum()) {
                size_t idx = accsNode->index<size_t>();
                Tuple *t   = frame->get<Tuple *>(srcIdx);
                ASSERT(idx < t->size(), "Tuple index out of bounds in Taskflow.");
                frame->set(n->index(), t->get<slot_t>(idx));
            } else {
                std::string key  = accsNode->index<std::string>();
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
            Graph *tgtGraph  = &tt::as_ptr<FuncNode>(n)->func()->graph();
            Frame *funcFrame = framePool_.acquire(tgtGraph);
            try {
                fillFrameForCall(funcFrame, tgtGraph, n, frame);
                instantiate_graph_instance_generic(sf, tgtGraph, funcFrame);
                sf.join();
                slot_t result = get_graph_return(tgtGraph, funcFrame);
                frame->set(n->index(), result);
                framePool_.release(funcFrame);
            } catch (...) {
                framePool_.release(funcFrame);
                throw;
            }
        })
        .name("FUNC");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCallTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
            Graph *tgtGraph  = frame->get<Function *>(n->withInputs().front()->index())->graph();
            Frame *funcFrame = framePool_.acquire(tgtGraph);
            try {
                fillFrameForCall(funcFrame, tgtGraph, n, frame);
                instantiate_graph_instance_generic(sf, tgtGraph, funcFrame);
                sf.join();
                slot_t result = get_graph_return(tgtGraph, funcFrame);
                frame->set(n->index(), result);
                framePool_.release(funcFrame);
            } catch (...) {
                framePool_.release(funcFrame);
                throw;
            }
        })
        .name("CALL");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildOperTask(FlowT &flowLike, Node *n, Frame *frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
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
                auto opFunc = context_->execMgr().find(uri);
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
                std::vector<data_idx_t> normIndices, withIndices;
                for (const auto &in : n->normInputs())
                    normIndices.push_back(in->index());
                for (const auto &in : n->withInputs())
                    withIndices.push_back(in->index());
                data_arr_t nargs{normIndices.data(), static_cast<size_t>(normIndices.size())};
                data_arr_t wargs{withIndices.data(), static_cast<size_t>(withIndices.size())};
                FrameArgsView withView(*frame, wargs);
                FrameArgsView normView(*frame, nargs);
                slot_t result;
                try {
                    result = (*opFunc)(withView, normView, *context_);
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
                frame->set(n->index(), result);
            }
        })
        .name("OPER");
}

template <typename FlowT>
void TaskflowExecSchedPass::buildBranchJoinRegion(
    FlowT &flowLike, Graph *graph, Frame *frame, std::unordered_map<Node *, tf::Task> &taskMap,
    Node *brch) {
    node_vec_t candidates = brch->ctrlOutputs();
    Node *join            = brch->dataOutputs().front();

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
            })
            .name("BRCH_SEL");

    auto joiner =
        flowLike.emplace([join, frame]() { (void)frame->get<slot_t>(join->index()); }).name("JOIN");

    auto precede_from_inputs = [&](const node_vec_t &inputs, tf::Task tsk) {
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
                    if (static_cast<size_t>(tarIdx) != i)
                        return;

                    slot_t out = NullSlot;
                    if (candidate->type() == NodeType::FUNC) {
                        Graph *tgtGraph  = &tt::as_ptr<FuncNode>(candidate)->func()->graph();
                        Frame *funcFrame = framePool_.acquire(tgtGraph);
                        try {
                            fillFrameForCall(funcFrame, tgtGraph, candidate, frame);
                            instantiate_graph_instance_generic(csf, tgtGraph, funcFrame);
                            csf.join();
                            out = get_graph_return(tgtGraph, funcFrame);
                            framePool_.release(funcFrame);
                        } catch (...) {
                            framePool_.release(funcFrame);
                            throw;
                        }
                    } else if (candidate->type() == NodeType::CALL) {
                        Graph *tgtGraph =
                            frame->get<Function *>(candidate->withInputs().front()->index())
                                ->graph();
                        Frame *funcFrame = framePool_.acquire(tgtGraph);
                        try {
                            fillFrameForCall(funcFrame, tgtGraph, candidate, frame);
                            instantiate_graph_instance_generic(csf, tgtGraph, funcFrame);
                            csf.join();
                            out = get_graph_return(tgtGraph, funcFrame);
                            framePool_.release(funcFrame);
                        } catch (...) {
                            framePool_.release(funcFrame);
                            throw;
                        }
                    } else if (candidate->type() == NodeType::OPER) {
                        auto opNode = tt::as_ptr<OperNode>(candidate);
                        auto uri    = opNode->oper()->uri();
                        auto opFunc = context_->execMgr().find(uri);
                        if (!opFunc) {
                            throw reportRuntimeFault(
                                *context_,
                                RuntimeFault::make(RuntimeDiag::UnrecognizedOperatorURI, uri),
                                makeNodeExecutionSite(
                                    context_->sourceContext(),
                                    &candidate->graph(),
                                    candidate,
                                    0,
                                    "taskflow",
                                    ExecutionSiteKind::TaskNode));
                        }
                        std::vector<data_idx_t> normIndices, withIndices;
                        for (const auto &in : candidate->normInputs())
                            normIndices.push_back(in->index());
                        for (const auto &in : candidate->withInputs())
                            withIndices.push_back(in->index());
                        data_arr_t nargs{
                            normIndices.data(),
                            static_cast<size_t>(normIndices.size())};
                        data_arr_t wargs{
                            withIndices.data(),
                            static_cast<size_t>(withIndices.size())};
                        FrameArgsView withView(*frame, wargs);
                        FrameArgsView normView(*frame, nargs);
                        try {
                            out = (*opFunc)(withView, normView, *context_);
                        } catch (const RuntimeFault &fault) {
                            throw reportRuntimeFault(
                                *context_,
                                fault,
                                makeNodeExecutionSite(
                                    context_->sourceContext(),
                                    &candidate->graph(),
                                    candidate,
                                    0,
                                    "taskflow",
                                    ExecutionSiteKind::TaskNode));
                        }
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
        if (skipNodes.count(n) && n->type() != NodeType::BRCH)
            continue;
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

    auto add_edges_from_inputs = [&](const node_vec_t &inputs, tf::Task tsk) {
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
    Array *arr               = frame->get<Array *>(node->normInputs().front()->index());
    Function *func           = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g                 = func->graph();
    Tuple *closure           = func->tuple();
    const auto &portNodes    = g->ports();
    const auto &closureNodes = g->closure();
    const size_t n           = arr->size();

    std::vector<slot_t> results(n);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, g, closure, &portNodes, &closureNodes, &results](
                       tf::Subflow &isf) {
              Frame *f = framePool_.acquire(g);
              if (!portNodes.empty())
                  f->set(portNodes[0]->index(), arr->get<slot_t>(i));
              for (size_t j = 0; j < closure->size() && j < closureNodes.size(); ++j)
                  f->set(closureNodes[j]->index(), closure->get<slot_t>(j));
              instantiate_graph_instance_generic(isf, g, f);
              isf.join();
              results[i] = get_graph_return(g, f);
              framePool_.release(f);
          }).name("MAP_ELEM");
    }
    sf.join();
    Array *res = Array::create(mm::autoSpace(), n);
    for (size_t i = 0; i < n; ++i)
        res->set(i, results[i]);
    frame->set(node->index(), res);
}

void TaskflowExecSchedPass::mark_apply_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr               = frame->get<Array *>(node->normInputs().front()->index());
    Function *func           = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g                 = func->graph();
    Tuple *closure           = func->tuple();
    const auto &portNodes    = g->ports();
    const auto &closureNodes = g->closure();
    const size_t n           = arr->size();

    std::vector<slot_t> results(n);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, g, closure, &portNodes, &closureNodes, &results](
                       tf::Subflow &isf) {
              Frame *f = framePool_.acquire(g);
              if (!portNodes.empty())
                  f->set(portNodes[0]->index(), arr->get<slot_t>(i));
              for (size_t j = 0; j < closure->size() && j < closureNodes.size(); ++j)
                  f->set(closureNodes[j]->index(), closure->get<slot_t>(j));
              instantiate_graph_instance_generic(isf, g, f);
              isf.join();
              results[i] = get_graph_return(g, f);
              framePool_.release(f);
          }).name("APPLY_ELEM");
    }
    sf.join();
    for (size_t i = 0; i < n; ++i)
        arr->set(i, results[i]);
    frame->set(node->index(), arr);
}

void TaskflowExecSchedPass::mark_filter_arr(Node *node, Frame *frame, tf::Subflow &sf) {
    Array *arr               = frame->get<Array *>(node->normInputs().front()->index());
    Function *func           = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g                 = func->graph();
    Tuple *closure           = func->tuple();
    const auto &portNodes    = g->ports();
    const auto &closureNodes = g->closure();
    const size_t n           = arr->size();

    std::vector<bool> keep(n, false);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, i, arr, g, closure, &portNodes, &closureNodes, &keep](tf::Subflow &isf) {
              Frame *f = framePool_.acquire(g);
              if (!portNodes.empty())
                  f->set(portNodes[0]->index(), arr->get<slot_t>(i));
              for (size_t j = 0; j < closure->size() && j < closureNodes.size(); ++j)
                  f->set(closureNodes[j]->index(), closure->get<slot_t>(j));
              instantiate_graph_instance_generic(isf, g, f);
              isf.join();
              slot_t result = get_graph_return(g, f);
              keep[i]       = fromSlot<bool>(result);
              framePool_.release(f);
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
    Array *arr               = frame->get<Array *>(node->normInputs().front()->index());
    Function *func           = frame->get<Function *>(node->withInputs()[0]->index());
    slot_t init              = frame->get<slot_t>(node->withInputs()[1]->index());
    Graph *g                 = func->graph();
    Tuple *closure           = func->tuple();
    const auto &portNodes    = g->ports();
    const auto &closureNodes = g->closure();

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
            sf.emplace([this, accPtr, elem, g, closure, &portNodes, &closureNodes](
                           tf::Subflow &isf) {
                  Frame *f = framePool_.acquire(g);
                  ASSERT(portNodes.size() >= 2, "reduce expects binary function (two ports).");
                  f->set(portNodes[0]->index(), *accPtr);
                  f->set(portNodes[1]->index(), elem);
                  for (size_t j = 0; j < closure->size() && j < closureNodes.size(); ++j)
                      f->set(closureNodes[j]->index(), closure->get<slot_t>(j));
                  instantiate_graph_instance_generic(isf, g, f);
                  isf.join();
                  *accPtr = get_graph_return(g, f);
                  framePool_.release(f);
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
    Array *arr               = frame->get<Array *>(node->normInputs().front()->index());
    Function *func           = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g                 = func->graph();
    Tuple *closure           = func->tuple();
    const auto &portNodes    = g->ports();
    const auto &closureNodes = g->closure();

    tf::Task prev;
    bool has_prev = false;
    for (size_t i = 0; i < arr->size(); ++i) {
        slot_t elem = arr->get<slot_t>(i);
        tf::Task step =
            sf.emplace([this, elem, g, closure, &portNodes, &closureNodes](tf::Subflow &isf) {
                  Frame *f = framePool_.acquire(g);
                  if (!portNodes.empty())
                      f->set(portNodes[0]->index(), elem);
                  for (size_t j = 0; j < closure->size() && j < closureNodes.size(); ++j)
                      f->set(closureNodes[j]->index(), closure->get<slot_t>(j));
                  instantiate_graph_instance_generic(isf, g, f);
                  isf.join();
                  (void)get_graph_return(g, f);
                  framePool_.release(f);
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
    Array *arr               = frame->get<Array *>(node->normInputs().front()->index());
    Function *func           = frame->get<Function *>(node->withInputs().front()->index());
    Graph *g                 = func->graph();
    Tuple *closure           = func->tuple();
    const auto &portNodes    = g->ports();
    const auto &closureNodes = g->closure();

    for (size_t i = 0; i < arr->size(); ++i) {
        sf.emplace([this, i, arr, g, closure, &portNodes, &closureNodes](tf::Subflow &isf) {
              Frame *f = framePool_.acquire(g);
              if (!portNodes.empty())
                  f->set(portNodes[0]->index(), arr->get<slot_t>(i));
              for (size_t j = 0; j < closure->size() && j < closureNodes.size(); ++j)
                  f->set(closureNodes[j]->index(), closure->get<slot_t>(j));
              instantiate_graph_instance_generic(isf, g, f);
              isf.join();
              (void)get_graph_return(g, f);
              framePool_.release(f);
          }).name("FOREACH_ELEM");
    }
    sf.join();
    frame->set(node->index(), NullSlot);
}
