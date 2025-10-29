#include "taskflow.h"
#include "core/data/composed/array.h"
#include "core/data/primary.h"

#include <fstream>
#include <iomanip>
#include <queue>
#include <regex>
#include <sstream>
#include <thread>
#include <type_traits>
#include <unordered_set>

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

using namespace std;
using namespace GraphIR;

graph_ptr_t TaskflowExecSchedPass::apply(graph_ptr_t &graph, std::ostream & /*os*/) {
    if (!graph->hasOutput()) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MissingMainFunction)
            .commit(context_->mainModule()->name());
    }
    auto rootFrame = new Frame(graph.get());

    // 构建元信息（统计所有图中的 BRCH-JOIN 对）
    buildGraphsInfo(graph.get());

    // 执行 main 子图实例
    evalGraphTF(graph.get(), rootFrame);

    return Graph::null();
}

inline data_ptr_t get_graph_return(Graph *g, const frame_rptr_t &frame) {
    const auto &retNode = g->exitNode();
    // 与 nodevm 保持一致：仅使用 normInputs 作为返回
    if (retNode->normInputs().empty()) {
        return Data::null();
    }
    auto in = retNode->normInputs().front();
    return frame->get(in->index());
}

data_ptr_t TaskflowExecSchedPass::evalGraphTF(Graph *graph, const frame_rptr_t &frame) {
    EXEC_WHEN_DEBUG(l.in("TF").debug("Evaluating graph (TF): {}", graph->name()));
    instantiate_graph_instance_generic(mainFlow_, graph, frame);
    EXEC_WHEN_DEBUG(l.in("TF").debug("Submitting taskflow and wait: {}", graph->name()));
    executor_.run(mainFlow_).wait();
    auto ret = get_graph_return(graph, frame);
    return ret;
}

void TaskflowExecSchedPass::buildGraphsInfo(Graph *rootGraph) {
    EXEC_WHEN_DEBUG(l.in("TF").debug("Building graph info for: {}", rootGraph->name()));
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

        // FUNC 节点和子图要递归进去分析节点，BRCH-JOIN 存入 map
        for (const auto &n : g->nodes()) {
            if (n->type() == NodeType::FUNC) {
                auto fn = tt::as_shared<FuncNode>(n);
                auto &sub = fn->func()->graph();
                if (!visited.count(&sub))
                    q.push(&sub);
            } else if (n->type() == NodeType::BRCH) {
                node_vec_t &candidates = n->ctrlOutputs();
                node_ptr_t join = n->dataOutputs().front();
                for (const auto &c : candidates) {
                    globalBuildCtx_.skipNodes.insert(c.get());
                }
                globalBuildCtx_.skipNodes.insert(n.get());
                globalBuildCtx_.skipNodes.insert(join.get());
                ASSERT(join->type() == NodeType::JOIN, "BRCH must be paired with JOIN.");

                gt.joinToBrch[join.get()] = n;
            }
        }

        // 子图
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
    FlowT &flowLike, Graph *graph, const frame_rptr_t &frame) {
    (void)frame;
    std::unordered_map<Node *, tf::Task> taskMap;
    buildNormalNodeTasks(flowLike, graph, frame, taskMap);
    connectDependencies(flowLike, graph, taskMap);

    EXEC_WHEN_DEBUG(
        l.in("TF").debug("Instantiated graph: {} (tasks={})", graph->name(), taskMap.size()));
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildExitTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_rptr_t &frame) {
    auto t = flowLike
                 .emplace([n, frame]() {
                     ASSERT(n->withInputs().size() == 0, "Return node cannot have with inputs.");
                     ASSERT(
                         n->normInputs().size() <= 1,
                         "Return node cannot have multiple norm inputs.");

                     if (!n->normInputs().empty()) {
                         auto in = n->normInputs().front();
                         auto v = frame->get(in->index());
                         if (v == nullptr) {
                             v = std::make_shared<IntData>(0);
                             frame->set(in->index(), v);
                         }
                     }
                 })
                 .name("EXIT");
    return t;
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildDataTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_rptr_t &frame) {
    return flowLike
        .emplace([n, frame]() {
            auto v = frame->get(n->index());
            ASSERT(v != nullptr, "DATA node should already have data.");
        })
        .name("DATA:" + regex_replace(n->toString(), regex("\""), "\\\""));
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildPortTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_rptr_t &frame) {
    return flowLike
        .emplace([n, frame]() {
            auto v = frame->get(n->index());
            ASSERT(v != nullptr, "PORT node should already have data.");
        })
        .name("PORT:" + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCopyTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_rptr_t &frame) {
    return flowLike
        .emplace([n, frame]() {
            auto src = n->normInputs().front();
            auto v = frame->get(src->index());
            if (v == nullptr) {
                v = std::make_shared<IntData>(0);
                frame->set(src->index(), v);
            }
            frame->set(n->index(), v->clone());
        })
        .name("COPY:" + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFillTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_rptr_t &frame) {
    return flowLike
        .emplace([n, frame]() {
            ASSERT(!n->normInputs().empty(), "FILL expects a source value on norm input.");
            const auto &srcNode = n->normInputs().front();
            data_ptr_t data = frame->get(srcNode->index());
            ASSERT(data != nullptr, "FILL source data is null.");
            data = data->clone();
            data_vec_t inputs;
            inputs.reserve(n->withInputs().size());
            for (const auto &w : n->withInputs()) {
                inputs.push_back(frame->get(w->index()));
            }
            // 交由具体数据类型实现（FunctionData 等）进行填充
            data->resolve(inputs);
            frame->set(n->index(), data);
        })
        .name("FILL:" + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildAccsTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_rptr_t &frame) {
    return flowLike
        .emplace([n, frame]() {
            data_ptr_t source = frame->get(n->dataInputs().front()->index());
            auto acc = tt::as_shared<AccsNode>(n);
            if (acc->isNum()) {
                size_t idx = acc->index<size_t>();
                auto a = tt::as_shared<TupleData>(source);
                ASSERT(idx < a->raw().size(), "Tuple index out of bounds.");
                frame->set(n->index(), a->raw()[idx]);
            } else {
                std::string key = acc->index<std::string>();
                auto d = tt::as_shared<StructData>(source);
                ASSERT(d->raw().contains(key), "Struct key not found.");
                frame->set(n->index(), d->raw().at(key));
            }
        })
        .name("ACCS:" + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFuncTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_rptr_t &frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
            auto func = tt::as_shared<FuncNode>(n)->func();
            auto &tgtGraph = func->graph();

            data_vec_t args;
            for (const auto &inNode : n->normInputs()) {
                auto v = frame->get(inNode->index());
                args.push_back(v);
            }
            const auto &ports = tgtGraph.normPorts();
            ASSERT(ports.size() == args.size(), "Argument count mismatch.");

            auto nextFrame = new Frame(&tgtGraph);
            for (size_t i = 0; i < ports.size(); ++i) {
                nextFrame->set(ports[i]->index(), args[i]);
            }

            // 注入闭包到 withPorts（捕获变量端口）
            const auto &withPorts = tgtGraph.withPorts();
            const auto &withIns = n->withInputs();
            ASSERT(withPorts.size() == withIns.size(), "Function with-ports size mismatch.");
            for (size_t i = 0; i < withPorts.size(); ++i) {
                auto v = frame->get(withIns[i]->index());
                nextFrame->set(withPorts[i]->index(), v);
            }

            // 注入闭包：callee.closure() <-> caller.withInputs()
            if (tgtGraph.hasClosure()) {
                const auto &closureNodes = tgtGraph.closure();
                const auto &withIns2 = n->withInputs();
                ASSERT(closureNodes.size() == withIns2.size(), "Function closure size mismatch.");
                for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                    auto v = frame->get(withIns2[ci]->index());
                    nextFrame->set(closureNodes[ci]->index(), v);
                }
            }

            this->instantiate_graph_instance_generic(sf, &tgtGraph, nextFrame);
            sf.join();

            auto result = get_graph_return(&tgtGraph, nextFrame);
            frame->set(n->index(), result);
        })
        .name(std::string("FUNC:") + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCallTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_rptr_t &frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
            const auto &funcNode = n->withInputs().front();
            const auto &funcData = frame->get(funcNode->index());
            auto functionData = tt::as_shared<FunctionData>(funcData);
            auto &tgtGraph = functionData->graph();

            // 仅绑定普通参数
            data_vec_t args;
            for (const auto &inNode : n->normInputs()) {
                auto v = frame->get(inNode->index());
                args.push_back(v);
            }
            const auto &ports = tgtGraph.normPorts();
            ASSERT(ports.size() == args.size(), "Argument count mismatch.");

            auto funcFrame = new Frame(&tgtGraph);
            for (size_t i = 0; i < ports.size(); ++i) {
                funcFrame->set(ports[i]->index(), args[i]);
            }

            // 参考 nodevm：优先写入 closure() 节点；withPorts 仅在尺寸匹配时写
            const auto &closureData = functionData->closure();
            if (tgtGraph.hasClosure()) {
                const auto &closureNodes = tgtGraph.closure();
                ASSERT(
                    closureNodes.size() == closureData.size(),
                    "Function closure size mismatch.");
                for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                    funcFrame->set(closureNodes[ci]->index(), closureData[ci]);
                }
            }
            const auto &withPorts = tgtGraph.withPorts();
            if (!withPorts.empty() && withPorts.size() == closureData.size()) {
                for (size_t wi = 0; wi < withPorts.size(); ++wi) {
                    funcFrame->set(withPorts[wi]->index(), closureData[wi]);
                }
            }

            this->instantiate_graph_instance_generic(sf, &tgtGraph, funcFrame);
            sf.join();

            auto result = get_graph_return(&tgtGraph, funcFrame);
            frame->set(n->index(), result);
        })
        .name(std::string("CALL:") + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildOperTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_rptr_t &frame) {
    return flowLike
        .emplace([n, frame, this](tf::Subflow &sf) {
            auto opNode = tt::as_shared<OperNode>(n);
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
                auto self = const_cast<node_ptr_t &>(n);
                context_->eval(uri, self, *frame);
            }
        })
        .name("OPER:" + n->toString());
}

template <typename FlowT>
void TaskflowExecSchedPass::buildBranchJoinRegion(
    FlowT &flowLike, Graph *graph, const frame_rptr_t &frame,
    std::unordered_map<Node *, tf::Task> &taskMap, const node_ptr_t &brch) {
    node_vec_t candidates = brch->ctrlOutputs();
    node_ptr_t join = brch->dataOutputs().front();
    (void)graph;

    auto selector = flowLike
                        .emplace([brch, frame]() {
                            auto condNode = brch->normInputs().front();
                            auto condData = frame->get(condNode->index());
                            auto withIns = brch->withInputs();
                            int tar = -1;
                            if (withIns.empty()) {
                                tar = (int)(condData->as<BoolData>(Type::Bool())->data());
                                tar = tar ? 0 : 1;
                            } else {
                                for (size_t j = 0; j < withIns.size(); ++j) {
                                    if (condData->equals(frame->get(withIns[j]->index()))) {
                                        tar = (int)j;
                                        break;
                                    }
                                }
                                if (tar == -1)
                                    tar = (int)withIns.size(); // default
                            }
                            ASSERT(tar >= 0, "Invalid branch target.");
                            frame->set(brch->index(), std::make_shared<IntData>(tar));
                            return tar;
                        })
                        .name("BRCH_SEL:" + brch->toString());

    // JOIN 仅作为同步点（不读取帧，由候选写回）
    auto joiner = flowLike
                      .emplace([brch, join, frame]() {
                          (void)frame->get(brch->index());
                          (void)join;
                      })
                      .name("JOIN:" + join->toString());

    auto precede_from_inputs = [&](const node_vec_t &inputs, tf::Task tsk) {
        for (const auto &in : inputs) {
            if (&in->graph() != graph)
                continue;
            if (globalBuildCtx_.skipNodes.count(in.get()) && in->type() != NodeType::JOIN)
                continue;
            auto it = taskMap.find(in.get());
            if (it != taskMap.end()) {
                it->second.precede(tsk);
            }
        }
    };

    // selector 依赖 BRCH 输入
    precede_from_inputs(brch->dataInputs(), selector);
    precede_from_inputs(brch->ctrlInputs(), selector);

    for (size_t i = 0; i < candidates.size(); ++i) {
        node_ptr_t candidate = candidates[i];
        auto layer = flowLike.emplace([]() {}).name("BRCH_CAND_LAYER");

        tf::Task task_do =
            flowLike
                .emplace([i, candidate, brch, frame, this](tf::Subflow &csf) {
                    auto tar = frame->get(brch->index());
                    int tar_idx = tar->as<IntData>(Type::Int())->data();
                    if ((int)i != tar_idx) {
                        return;
                    }

                    data_ptr_t out;
                    if (candidate->type() == NodeType::FUNC) {
                        auto func = tt::as_shared<FuncNode>(candidate)->func();
                        auto &fg = func->graph();

                        // 仅绑定普通参数
                        data_vec_t args;
                        for (const auto &inNode : candidate->normInputs()) {
                            auto v = frame->get(inNode->index());
                            args.push_back(v);
                        }
                        const auto &ports = fg.normPorts();
                        ASSERT(ports.size() == args.size(), "Argument count mismatch.");

                        auto fframe = new Frame(&fg);
                        for (size_t pi = 0; pi < ports.size(); ++pi) {
                            fframe->set(ports[pi]->index(), args[pi]);
                        }

                        // withPorts <- caller.withInputs()
                        const auto &withPorts = fg.withPorts();
                        const auto &withIns = candidate->withInputs();
                        ASSERT(
                            withPorts.size() == withIns.size(),
                            "Function with-ports size mismatch.");
                        for (size_t wi = 0; wi < withPorts.size(); ++wi) {
                            auto v = frame->get(withIns[wi]->index());
                            fframe->set(withPorts[wi]->index(), v);
                        }

                        // 注入闭包：callee.closure() <-> caller.withInputs()
                        if (fg.hasClosure()) {
                            const auto &closureNodes = fg.closure();
                            const auto &withIns2 = candidate->withInputs();
                            ASSERT(
                                closureNodes.size() == withIns2.size(),
                                "Function closure size mismatch.");
                            for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                                auto v = frame->get(withIns2[ci]->index());
                                fframe->set(closureNodes[ci]->index(), v);
                            }
                        }

                        this->instantiate_graph_instance_generic(csf, &fg, fframe);
                        csf.join();
                        out = get_graph_return(&fg, fframe);
                    } else if (candidate->type() == NodeType::CALL) {
                        const auto &funcNode = candidate->withInputs().front();
                        const auto &funcData = frame->get(funcNode->index());
                        auto &fg = tt::as_shared<FunctionData>(funcData)->graph();

                        data_vec_t args;
                        for (const auto &inNode : candidate->normInputs()) {
                            auto v = frame->get(inNode->index());
                            args.push_back(v);
                        }

                        const auto &ports = fg.normPorts();
                        ASSERT(ports.size() == args.size(), "Argument count mismatch.");

                        auto fframe = new Frame(&fg);
                        for (size_t pi = 0; pi < ports.size(); ++pi) {
                            fframe->set(ports[pi]->index(), args[pi]);
                        }

                        // withPorts <- FunctionData.closure()
                        const auto &functionData = tt::as_shared<FunctionData>(funcData);
                        const auto &closureData = functionData->closure();
                        const auto &withPorts = fg.withPorts();
                        ASSERT(
                            withPorts.size() == closureData.size(),
                            "Function with-ports size mismatch.");
                        for (size_t wi = 0; wi < withPorts.size(); ++wi) {
                            fframe->set(withPorts[wi]->index(), closureData[wi]);
                        }

                        if (fg.hasClosure()) {
                            const auto &closureNodes = fg.closure();
                            ASSERT(
                                closureNodes.size() == closureData.size(),
                                "Function closure size mismatch.");
                            for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                                fframe->set(closureNodes[ci]->index(), closureData[ci]);
                            }
                        }

                        this->instantiate_graph_instance_generic(csf, &fg, fframe);
                        csf.join();
                        out = get_graph_return(&fg, fframe);
                    } else if (candidate->type() == NodeType::OPER) {
                        auto self = const_cast<node_ptr_t &>(candidate);
                        context_->eval(
                            tt::as_shared<OperNode>(candidate)->oper()->uri(),
                            self,
                            *frame);
                        out = frame->get(candidate->index());
                    } else {
                        ASSERT(false, "Unsupported candidate node type in BRCH-JOIN.");
                    }

                    frame->set(candidate->index(), out);
                    frame->set(brch->dataOutputs().front()->index(), out); // 提前写回 JOIN 值
                })
                .name(std::string("BRCH_CAND_EXEC:") + candidate->toString());

        selector.precede(layer);
        layer.precede(task_do);
        task_do.precede(joiner);
    }

    taskMap[brch.get()] = selector;
    taskMap[join.get()] = joiner;
}

template <typename FlowT>
void TaskflowExecSchedPass::buildNormalNodeTasks(
    FlowT &flowLike, Graph *graph, const frame_rptr_t &frame,
    std::unordered_map<Node *, tf::Task> &taskMap) {
    std::unordered_set<Node *> &skipNodes = globalBuildCtx_.skipNodes;
    for (const auto &n : graph->nodes()) {
        if (skipNodes.count(n.get()) && n->type() != NodeType::BRCH)
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
        case NodeType::BRCH:
            buildBranchJoinRegion(flowLike, graph, frame, taskMap, n);
            continue; // 避免下面 taskMap[n.get()] = t 覆盖
        default:
            ASSERT(false, "Unsupported node type.");
        }
        taskMap[n.get()] = t;
    }

    // 端口节点（不包含在 nodes() 中，特殊处理）
    for (const auto &port : graph->withPorts()) {
        auto t = buildPortTask(flowLike, port, frame);
        taskMap[port.get()] = t;
    }
    for (const auto &port : graph->normPorts()) {
        auto t = buildPortTask(flowLike, port, frame);
        taskMap[port.get()] = t;
    }
    // exit 节点特殊处理，不在 nodes() 中
    auto exitNode = graph->exitNode();
    auto t = buildExitTask(flowLike, exitNode, frame);
    taskMap[exitNode.get()] = t;
}

template <typename FlowT>
void TaskflowExecSchedPass::connectDependencies(
    FlowT &flow, Graph *graph, std::unordered_map<Node *, tf::Task> &taskMap) {
    std::unordered_set<Node *> &skipNodes = globalBuildCtx_.skipNodes;

    auto add_edges_from_inputs = [&](const node_vec_t &inputs, tf::Task tsk) {
        for (const auto &in : inputs) {
            if (&in->graph() != graph)
                continue;
            if (skipNodes.count(in.get()) && in->type() != NodeType::JOIN)
                continue;
            auto it = taskMap.find(in.get());
            if (it != taskMap.end()) {
                it->second.precede(tsk);
            }
        }
    };

    for (const auto &n : graph->nodes()) {
        // 完全跳过 BRCH，由 BRCH-JOIN 区域连接内部依赖
        if (n->type() == NodeType::BRCH)
            continue;
        if (skipNodes.count(n.get()))
            continue;

        auto it = taskMap.find(n.get());
        if (it == taskMap.end())
            continue;
        auto tsk = it->second;

        add_edges_from_inputs(n->dataInputs(), tsk);
        add_edges_from_inputs(n->ctrlInputs(), tsk);
    }

    // exit 节点
    auto exitNode = graph->exitNode();
    auto it = taskMap.find(exitNode.get());
    if (it != taskMap.end()) {
        auto tsk = it->second;
        // dataInputs 同时承载 with/norm 的执行前置，连接 dataInputs/ctrlInputs 即可
        add_edges_from_inputs(exitNode->dataInputs(), tsk);
        add_edges_from_inputs(exitNode->ctrlInputs(), tsk);
    }
}

void TaskflowExecSchedPass::mark_map_arr(
    const node_ptr_t &node, frame_rptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front()->index());
    auto funcData = frame->get(node->withInputs().front()->index());

    auto func = funcData->as<FunctionData>(Type::Func());
    type_ptr_t funcRetType = func->funcType()->exitType();

    auto spawn_unary = [this, &sf, func, funcData](data_ptr_t arg, data_ptr_t &out_slot) {
        sf.emplace([this, arg, func, funcData, &out_slot](tf::Subflow &isf) {
              auto &fg = func->graph();
              auto fframe = new Frame(&fg);

              if (fg.hasClosure()) {
                  const auto &functionData = tt::as_shared<FunctionData>(funcData);
                  const auto &closureNodes = fg.closure();
                  const auto &closureData = functionData->closure();
                  ASSERT(
                      closureNodes.size() == closureData.size(),
                      "Function closure size mismatch.");
                  for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                      fframe->set(closureNodes[ci]->index(), closureData[ci]);
                  }
              }

              const auto &ports = fg.normPorts();
              ASSERT(ports.size() == 1, "Function should have exactly one parameter for map.");
              fframe->set(ports[0]->index(), arg);

              this->instantiate_graph_instance_generic(isf, &fg, fframe);
              isf.join();

              out_slot = get_graph_return(&fg, fframe);
          }).name("MAP_ELEM");
    };

    auto arr = tt::as_shared<ArrayData>(targetData);
    const auto &elements = arr->raw();
    const size_t n = elements.size();
    data_vec_t results(n);
    for (size_t i = 0; i < n; ++i) {
        spawn_unary(elements[i], results[i]);
    }
    sf.join();
    frame->set(node->index(), ArrayData::from(Type::Array(funcRetType), std::move(results)));
}

void TaskflowExecSchedPass::mark_apply_arr(
    const node_ptr_t &node, frame_rptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front()->index());
    auto funcData = frame->get(node->withInputs().front()->index());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto spawn_unary = [this, &sf, func, funcData](data_ptr_t arg, data_ptr_t &out_slot) {
        sf.emplace([this, arg, func, funcData, &out_slot](tf::Subflow &isf) {
              auto &fg = func->graph();
              auto fframe = new Frame(&fg);

              if (fg.hasClosure()) {
                  const auto &functionData = tt::as_shared<FunctionData>(funcData);
                  const auto &closureNodes = fg.closure();
                  const auto &closureData = functionData->closure();
                  ASSERT(
                      closureNodes.size() == closureData.size(),
                      "Function closure size mismatch.");
                  for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                      fframe->set(closureNodes[ci]->index(), closureData[ci]);
                  }
              }

              const auto &ports = fg.normPorts();
              ASSERT(ports.size() == 1, "apply expects unary function.");
              fframe->set(ports[0]->index(), arg);

              this->instantiate_graph_instance_generic(isf, &fg, fframe);
              isf.join();

              out_slot = get_graph_return(&fg, fframe);
          }).name("APPLY_ELEM");
    };

    auto par_apply = [&sf, &frame, node, spawn_unary](const data_vec_t &elements, auto createOut) {
        data_vec_t results(elements.size());
        for (size_t i = 0; i < elements.size(); ++i) {
            spawn_unary(elements[i], results[i]);
        }
        sf.join();
        frame->set(node->index(), createOut(std::move(results)));
    };

    auto arr = tt::as_shared<ArrayData>(targetData);
    par_apply(arr->raw(), [&](data_vec_t v) { return ArrayData::from(arr->type(), std::move(v)); });
}

void TaskflowExecSchedPass::mark_filter_arr(
    const node_ptr_t &node, frame_rptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front()->index());
    auto funcData = frame->get(node->withInputs().front()->index());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto par_filter = [this, &sf, &frame, node, func, funcData](
                          const data_vec_t &elements,
                          auto createOut,
                          auto elemType) {
        const size_t n = elements.size();
        std::vector<bool> keep(n, false);

        auto spawn_pred = [this, &sf, func, funcData, &keep](data_ptr_t arg, size_t idx) {
            sf.emplace([this, arg, func, funcData, &keep, idx](tf::Subflow &isf) {
                  auto &fg = func->graph();
                  auto fframe = new Frame(&fg);

                  if (fg.hasClosure()) {
                      const auto &functionData = tt::as_shared<FunctionData>(funcData);
                      const auto &closureNodes = fg.closure();
                      const auto &closureData = functionData->closure();
                      ASSERT(
                          closureNodes.size() == closureData.size(),
                          "Function closure size mismatch.");
                      for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                          fframe->set(closureNodes[ci]->index(), closureData[ci]);
                      }
                  }

                  const auto &ports = fg.normPorts();
                  ASSERT(ports.size() == 1, "filter expects unary predicate.");
                  fframe->set(ports[0]->index(), arg);

                  this->instantiate_graph_instance_generic(isf, &fg, fframe);
                  isf.join();

                  auto r = get_graph_return(&fg, fframe);
                  keep[idx] = r->as<BoolData>(Type::Bool())->data();
              }).name("FILTER_PRED");
        };

        for (size_t i = 0; i < n; ++i) {
            spawn_pred(elements[i], i);
        }
        sf.join();

        data_vec_t out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i)
            if (keep[i])
                out.push_back(elements[i]);
        frame->set(node->index(), createOut(elemType, std::move(out)));
    };

    auto arr = tt::as_shared<ArrayData>(targetData);
    par_filter(
        arr->raw(),
        [](auto t, data_vec_t v) { return ArrayData::from(t, std::move(v)); },
        arr->type());
}

void TaskflowExecSchedPass::mark_reduce_arr(
    const node_ptr_t &node, frame_rptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front()->index());
    auto funcData = frame->get(node->withInputs()[0]->index());
    auto initData = frame->get(node->withInputs()[1]->index());
    auto func = funcData->as<FunctionData>(Type::Func());

    data_vec_t elements = tt::as_shared<ArrayData>(targetData)->raw();

    if (elements.empty()) {
        frame->set(node->index(), initData);
        return;
    }

    // 顺序左折叠，但每一步用 subflow 执行函数图；通过任务链保证顺序
    data_ptr_t acc = initData;
    tf::Task prev;
    bool has_prev = false;
    for (size_t i = 0; i < elements.size(); ++i) {
        auto step =
            sf.emplace([this, &acc, i, &elements, func, funcData](tf::Subflow &isf) {
                  auto &fg = func->graph();
                  auto fframe = new Frame(&fg);

                  if (fg.hasClosure()) {
                      const auto &functionData = tt::as_shared<FunctionData>(funcData);
                      const auto &closureNodes = fg.closure();
                      const auto &closureData = functionData->closure();
                      ASSERT(
                          closureNodes.size() == closureData.size(),
                          "Function closure size mismatch.");
                      for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                          fframe->set(closureNodes[ci]->index(), closureData[ci]);
                      }
                  }

                  const auto &ports = fg.normPorts();
                  ASSERT(ports.size() == 2, "Binary function should have exactly two parameters.");
                  fframe->set(ports[0]->index(), acc);
                  fframe->set(ports[1]->index(), elements[i]);

                  this->instantiate_graph_instance_generic(isf, &fg, fframe);
                  isf.join();

                  acc = get_graph_return(&fg, fframe);
              }).name("REDUCE_STEP");
        if (has_prev)
            step.succeed(prev);
        prev = step;
        has_prev = true;
    }
    sf.join();
    frame->set(node->index(), acc);
}

void TaskflowExecSchedPass::mark_unordered_reduce_arr(
    const node_ptr_t &node, frame_rptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front()->index());
    auto funcData = frame->get(node->withInputs()[0]->index());
    auto initData = frame->get(node->withInputs()[1]->index());
    auto func = funcData->as<FunctionData>(Type::Func());

    data_vec_t elements = tt::as_shared<ArrayData>(targetData)->raw();

    if (elements.empty()) {
        frame->set(node->index(), initData);
        return;
    }

    constexpr size_t THRESH = 100;

    auto reduce_dc =
        [this,
         &sf,
         &elements,
         func,
         funcData](auto &&self, size_t l, size_t r, std::shared_ptr<data_ptr_t> out) -> tf::Task {
        const size_t n = r - l;
        if (n <= THRESH) {
            // 叶子：在线性顺序上在当前子流中逐步计算
            return sf
                .emplace([this, l, r, out, func, funcData, &elements](tf::Subflow &isf) {
                    data_ptr_t res = elements[l];
                    tf::Task prev;
                    bool has_prev = false;
                    for (size_t i = l + 1; i < r; ++i) {
                        auto step =
                            isf.emplace([this, i, &res, func, funcData, &elements](
                                            tf::Subflow &lsf) {
                                   auto &fg = func->graph();
                                   auto fframe = new Frame(&fg);

                                   // 注入闭包
                                   if (fg.hasClosure()) {
                                       const auto &functionData =
                                           tt::as_shared<FunctionData>(funcData);
                                       const auto &closureNodes = fg.closure();
                                       const auto &closureData = functionData->closure();
                                       ASSERT(
                                           closureNodes.size() == closureData.size(),
                                           "Function closure size mismatch.");
                                       for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                                           fframe->set(closureNodes[ci]->index(), closureData[ci]);
                                       }
                                   }

                                   const auto &ports = fg.normPorts();
                                   ASSERT(
                                       ports.size() == 2,
                                       "Binary function should have exactly two parameters.");
                                   fframe->set(ports[0]->index(), res);
                                   fframe->set(ports[1]->index(), elements[i]);

                                   this->instantiate_graph_instance_generic(lsf, &fg, fframe);
                                   lsf.join();

                                   res = get_graph_return(&fg, fframe);
                               })
                                .name("REDUCE_STEP");
                        if (has_prev)
                            step.succeed(prev);
                        prev = step;
                        has_prev = true;
                    }
                    isf.join();
                    *out = res;
                })
                .name("REDUCE_LEAF");
        } else {
            const size_t m = l + n / 2;
            auto left_out = std::make_shared<data_ptr_t>();
            auto right_out = std::make_shared<data_ptr_t>();

            tf::Task tl = self(self, l, m, left_out);
            tf::Task tr = self(self, m, r, right_out);

            auto tc =
                sf.emplace([this, left_out, right_out, out, func, funcData](tf::Subflow &isf) {
                      // 合并两个子区间：保持 left 在前、right 在后
                      auto &fg = func->graph();
                      auto fframe = new Frame(&fg);

                      // 注入闭包
                      if (fg.hasClosure()) {
                          const auto &functionData = tt::as_shared<FunctionData>(funcData);
                          const auto &closureNodes = fg.closure();
                          const auto &closureData = functionData->closure();
                          ASSERT(
                              closureNodes.size() == closureData.size(),
                              "Function closure size mismatch.");
                          for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                              fframe->set(closureNodes[ci]->index(), closureData[ci]);
                          }
                      }

                      const auto &ports = fg.normPorts();
                      ASSERT(
                          ports.size() == 2,
                          "Binary function should have exactly two parameters.");
                      fframe->set(ports[0]->index(), *left_out);
                      fframe->set(ports[1]->index(), *right_out);

                      this->instantiate_graph_instance_generic(isf, &fg, fframe);
                      isf.join();

                      *out = get_graph_return(&fg, fframe);
                  }).name("REDUCE_COMBINE");
            tc.succeed(tl, tr);
            return tc;
        }
    };

    auto total = std::make_shared<data_ptr_t>();
    tf::Task root = reduce_dc(reduce_dc, 0, elements.size(), total);

    // 最终与 initData 结合（init 在左，整体结果在右）
    auto final =
        sf.emplace([this, total, initData, &frame, node, func, funcData](tf::Subflow &isf) {
              auto &fg = func->graph();
              auto fframe = new Frame(&fg);

              // 注入闭包
              if (fg.hasClosure()) {
                  const auto &functionData = tt::as_shared<FunctionData>(funcData);
                  const auto &closureNodes = fg.closure();
                  const auto &closureData = functionData->closure();
                  ASSERT(
                      closureNodes.size() == closureData.size(),
                      "Function closure size mismatch.");
                  for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                      fframe->set(closureNodes[ci]->index(), closureData[ci]);
                  }
              }

              const auto &ports = fg.normPorts();
              ASSERT(ports.size() == 2, "Binary function should have exactly two parameters.");
              fframe->set(ports[0]->index(), initData);
              fframe->set(ports[1]->index(), *total);

              this->instantiate_graph_instance_generic(isf, &fg, fframe);
              isf.join();

              auto res = get_graph_return(&fg, fframe);
              frame->set(node->index(), res);
          }).name("REDUCE_FINAL");
    final.succeed(root);
    sf.join();
}

void TaskflowExecSchedPass::mark_foreach_arr(
    const node_ptr_t &node, frame_rptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front()->index());
    auto funcData = frame->get(node->withInputs().front()->index());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto add_step =
        [this, &sf, func, funcData](const data_ptr_t &arg, tf::Task &prev, bool &has_prev) {
            auto step = sf.emplace([this, arg, func, funcData](tf::Subflow &isf) {
                              auto &fg = func->graph();
                              auto fframe = new Frame(&fg);

                              if (fg.hasClosure()) {
                                  const auto &functionData = tt::as_shared<FunctionData>(funcData);
                                  const auto &closureNodes = fg.closure();
                                  const auto &closureData = functionData->closure();
                                  ASSERT(
                                      closureNodes.size() == closureData.size(),
                                      "Function closure size mismatch.");
                                  for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                                      fframe->set(closureNodes[ci]->index(), closureData[ci]);
                                  }
                              }

                              const auto &ports = fg.normPorts();
                              ASSERT(ports.size() == 1, "foreach expects unary function.");
                              fframe->set(ports[0]->index(), arg);

                              this->instantiate_graph_instance_generic(isf, &fg, fframe);
                              isf.join();

                              (void)get_graph_return(&fg, fframe);
                          }).name("FOREACH_ELEM_SYNC");
            if (has_prev) {
                step.succeed(prev);
            }
            prev = step;
            has_prev = true;
        };

    tf::Task prev;
    bool has_prev = false;

    const auto &elems = tt::as_shared<ArrayData>(targetData)->raw();
    for (const auto &e : elems)
        add_step(e, prev, has_prev);
    sf.join();
    frame->set(node->index(), Data::null());
}

void TaskflowExecSchedPass::mark_unordered_foreach_arr(
    const node_ptr_t &node, frame_rptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front()->index());
    auto funcData = frame->get(node->withInputs().front()->index());
    ASSERT(funcData->type()->code() == TypeCode::Function, "foreach expects a function.");
    auto func = funcData->as<FunctionData>(Type::Func());

    auto spawn_unary_void = [this, &sf, func, funcData](data_ptr_t arg) {
        sf.emplace([this, arg, func, funcData](tf::Subflow &isf) {
              auto &fg = func->graph();
              auto fframe = new Frame(&fg);

              // 注入闭包
              if (fg.hasClosure()) {
                  const auto &functionData = tt::as_shared<FunctionData>(funcData);
                  const auto &closureNodes = fg.closure();
                  const auto &closureData = functionData->closure();
                  ASSERT(
                      closureNodes.size() == closureData.size(),
                      "Function closure size mismatch.");
                  for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                      fframe->set(closureNodes[ci]->index(), closureData[ci]);
                  }
              }

              const auto &ports = fg.normPorts();
              ASSERT(ports.size() == 1, "foreach expects unary function.");
              fframe->set(ports[0]->index(), arg);

              this->instantiate_graph_instance_generic(isf, &fg, fframe);
              isf.join();

              (void)get_graph_return(&fg, fframe);
          }).name("FOREACH_ELEM");
    };

    auto elems = tt::as_shared<ArrayData>(targetData)->raw();
    for (auto &e : elems)
        spawn_unary_void(e);
    sf.join();
    frame->set(node->index(), Data::null());
}