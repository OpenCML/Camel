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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "taskflow.h"
#include "core/data/composed/array.h"
#include "core/data/primary.h"

#include <fstream>
#include <queue>
#include <thread>
#include <unordered_set>

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

using namespace std;
using namespace GraphIR;

graph_ptr_t TaskflowExecSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    auto optMainGraph = graph->getSubGraphsByName("main");
    ASSERT(optMainGraph.has_value(), "Main graph not found.");
    auto mainGraphSet = optMainGraph.value();
    ASSERT(!mainGraphSet.empty(), "Main graph set is empty.");
    ASSERT(mainGraphSet.size() == 1, "Multiple main graphs found.");
    auto mainGraph = *mainGraphSet.begin();
    auto rootFrame = Frame::create(nullptr, *graph);

    // 构建元信息（目前是统计所有图中的 BRCH-JOIN 对）
    buildGraphsInfo(graph.get());

    // 执行 main 子图实例，理论上来说应该是不存在修改父栈帧的情况的，否则会有线程冲突
    auto mainFrame = Frame::create(rootFrame, *mainGraph);
    evalGraphTF(mainGraph.get(), mainFrame);

    return Graph::null();
}

inline data_ptr_t get_graph_return(Graph *g, const frame_ptr_t &frame) {
    const auto &retNode = g->exitNode();
    if (retNode->normInputs().empty()) {
        return Data::null();
    }
    return frame->get(retNode->normInputs().front());
}

data_ptr_t TaskflowExecSchedPass::evalGraphTF(Graph *graph, const frame_ptr_t &frame) {
    EXEC_WHEN_DEBUG(l.in("TF").debug("Evaluating graph (TF): {}", graph->name()));
    tf::Taskflow flow;
    instantiate_graph_instance_generic(flow, graph, frame);
    executor_.run(flow).wait();
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

        // FUNC 节点和子图要递归进去分析节点，BRCH-JOIN 存入 map
        for (const auto &n : g->nodes()) {
            if (n->type() == NodeType::FUNC) {
                auto fn = tt::as_shared<FuncNode>(n);
                auto &sub = fn->func()->graph();
                if (!visited.count(&sub))
                    q.push(&sub);
            } else if (n->type() == NodeType::BRCH) {
                node_vec_t &candidates = n->ctrlOutputs();
                node_ptr_t join = candidates.front()->ctrlOutputs().front();

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
            for (const auto &g : graphsSet) {
                if (!visited.count(g.get()))
                    q.push(g.get());
            }
        }
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::instantiate_graph_instance_generic(
    FlowT &flowLike, Graph *graph, const frame_ptr_t &frame) {
    // 往动态递归开的subflow / main图的taskflow中建立任务流
    std::unordered_map<Node *, tf::Task> taskMap;
    buildNormalNodeTasks(flowLike, graph, frame, taskMap);
    buildBranchJoinRegions(flowLike, graph, frame, taskMap);
    connectDependencies(flowLike, graph, taskMap);
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildExitTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_ptr_t &frame) {
    auto t =
        flowLike
            .emplace([n, frame]() {
                ASSERT(n->withInputs().size() == 0, "Exit node cant have with inputs.");
                ASSERT(n->normInputs().size() <= 1, "Exit node cant have multiple norm inputs.");
            })
            .name("EXIT");
    return t;
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildDataTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_ptr_t &frame) {
    return flowLike
        .emplace([n, frame]() {
            ASSERT(frame->get(n) != nullptr, "DATA node should already have data.");
        })
        .name("DATA:" + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildPortTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_ptr_t &frame) {
    return flowLike
        .emplace([n, frame]() {
            ASSERT(frame->get(n) != nullptr, "PORT node should already have data.");
        })
        .name("PORT:" + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCopyTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_ptr_t &frame) {
    return flowLike
        .emplace([n, frame]() {
            auto src = n->withInputs().front();
            auto v = frame->get(src);
            if (v == nullptr) {
                v = std::make_shared<Int32Data>(0);
                frame->set(src, v);
            }
            frame->set(n, v->clone());
        })
        .name("COPY:" + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFillTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_ptr_t &frame) {
    return flowLike
        .emplace([n, frame]() {
            const auto &srcNode = n->withInputs().front();
            const auto &dataInputs = n->normInputs();
            data_ptr_t data = frame->get(srcNode)->clone();
            ASSERT(data != nullptr, "FILL data is null.");
            data_vec_t inputs;
            inputs.reserve(dataInputs.size());
            for (const auto &input : dataInputs)
                inputs.push_back(frame->get(input));
            data->resolve(inputs);
            frame->set(n, data);
        })
        .name("FILL:" + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildAccsTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_ptr_t &frame) {
    return flowLike
        .emplace([this, n, frame]() {
            data_ptr_t source = frame->get(n->dataInputs().front());
            auto acc = tt::as_shared<AccsNode>(n);
            if (acc->isNum()) {
                size_t idx = acc->index<size_t>();
                if (source->type()->code() == TypeCode::Array) {
                    auto a = tt::as_shared<ArrayData>(source);
                    ASSERT(idx < a->raw().size(), "Array index out of bounds.");
                    frame->set(n, a->raw()[idx]);
                } else if (source->type()->code() == TypeCode::Tuple) {
                    auto t = tt::as_shared<TupleData>(source);
                    ASSERT(idx < t->raw().size(), "Tuple index out of bounds.");
                    frame->set(n, t->raw()[idx]);
                } else {
                    context_->rtmDiags()
                        ->of(RuntimeDiag::IncompatibleArgType)
                        .commit(0, "ACCS", "Array/Tuple/Vector", source->type()->toString());
                    throw CamelRuntimeException(
                        RuntimeExceptionCode::InvalidWithParameter,
                        "Incorrect args.");
                }
            } else {
                std::string key = acc->index<std::string>();
                if (source->type()->code() == TypeCode::Struct) {
                    auto d = tt::as_shared<StructData>(source);
                    ASSERT(d->raw().contains(key), "Struct key not found.");
                    frame->set(n, d->raw().at(key));
                } else {
                    context_->rtmDiags()
                        ->of(RuntimeDiag::IncompatibleArgType)
                        .commit(0, "ACCS", "Struct", source->type()->toString());
                    throw CamelRuntimeException(
                        RuntimeExceptionCode::InvalidWithParameter,
                        "Incorrect args.");
                }
            }
        })
        .name("ACCS:" + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFuncTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_ptr_t &frame) {
    // 使用动态子流递归展开子图
    return flowLike
        .emplace([this, n, frame](tf::Subflow &sf) {
            auto func = tt::as_shared<FuncNode>(n)->func();
            auto &tgtGraph = func->graph();
            data_vec_t args;
            for (const auto &inNode : n->dataInputs())
                args.push_back(frame->get(inNode));
            const auto &ports = tgtGraph.ports();
            ASSERT(ports.size() == args.size(), "Argument count mismatch.");

            auto nextFrame = Frame::create(frame, tgtGraph);
            for (size_t i = 0; i < ports.size(); ++i)
                nextFrame->set(ports[i], args[i]);

            instantiate_graph_instance_generic(sf, &tgtGraph, nextFrame);
            sf.join();

            auto result = get_graph_return(&tgtGraph, nextFrame);
            frame->set(n, result);
        })
        .name(std::string("FUNC:") + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCallTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_ptr_t &frame) {
    return flowLike
        .emplace([this, n, frame](tf::Subflow &sf) {
            const auto &funcNode = n->withInputs().front();
            const auto &funcData = frame->get(funcNode);
            auto &tgtGraph = tt::as_shared<FunctionData>(funcData)->graph();

            data_vec_t args;
            for (const auto &inNode : n->normInputs())
                args.push_back(frame->get(inNode));
            const auto &ports = tgtGraph.ports();
            ASSERT(ports.size() == args.size(), "Argument count mismatch.");

            auto funcFrame = Frame::create(frame, tgtGraph);
            for (size_t i = 0; i < ports.size(); ++i)
                funcFrame->set(ports[i], args[i]);

            instantiate_graph_instance_generic(sf, &tgtGraph, funcFrame);
            sf.join();

            auto result = get_graph_return(&tgtGraph, funcFrame);
            frame->set(n, result);
        })
        .name(std::string("CALL:") + n->toString());
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildOperTask(
    FlowT &flowLike, const node_ptr_t &n, const frame_ptr_t &frame) {
    return flowLike
        .emplace([this, n, frame](tf::Subflow &sf) {
            auto opNode = tt::as_shared<OperNode>(n);
            const auto &uri = opNode->oper()->uri();
            if (uri.starts_with(":mark/")) {
                if (uri == ":mark/map_arr") {
                    mark_map(n, frame, sf);
                } else if (uri == ":mark/apply_arr") {
                    mark_apply(n, frame, sf);
                } else if (uri == ":mark/filter_arr") {
                    mark_filter(n, frame, sf);
                } else if (uri == ":mark/reduce_arr") {
                    mark_reduce(n, frame, sf);
                } else if (uri == ":mark/foreach_arr") {
                    mark_foreach(n, frame, sf);
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
void TaskflowExecSchedPass::buildBranchJoinRegions(
    FlowT &flowLike, Graph *graph, const frame_ptr_t &frame,
    std::unordered_map<Node *, tf::Task> &taskMap) {

    auto &meta = globalBuildCtx_.getGraphInfos(graph);
    std::unordered_map<Node *, node_ptr_t> &joinToBrch = meta.joinToBrch;

    // 为每个 BRCH-JOIN 区域建立 selector / candidates / joiner 任务
    for (auto &kv : joinToBrch) {
        node_ptr_t brch = kv.second;
        node_vec_t candidates = brch->ctrlOutputs();
        node_ptr_t join = candidates.front()->ctrlOutputs().front();

        auto selector = flowLike
                            .emplace([brch, frame]() {
                                auto condNode = brch->withInputs().front();
                                auto condData = frame->get(condNode);
                                auto normIns = brch->normInputs();
                                int tar = -1;
                                if (normIns.empty()) {
                                    tar = (int)(condData->as<BoolData>(Type::Bool())->data());
                                    tar = tar ? 0 : 1;
                                } else {
                                    for (size_t j = 0; j < normIns.size(); ++j) {
                                        if (condData->equals(frame->get(normIns[j]))) {
                                            tar = (int)j;
                                            break;
                                        }
                                    }
                                    if (tar == -1)
                                        tar = (int)normIns.size(); // default
                                }
                                ASSERT(tar >= 0, "Invalid branch target.");
                                frame->set(brch, std::make_shared<Int32Data>(tar));
                                return tar;
                            })
                            .name("BRCH_SEL:" + brch->toString());

        auto joiner = flowLike
                          .emplace([brch, join, frame]() {
                              auto tar = frame->get(brch);
                              int tar_idx = tar->as<Int32Data>(Type::Int32())->data();
                              auto ctrlIns = join->ctrlInputs();
                              auto res = frame->get(ctrlIns[tar_idx]);
                              frame->set(join, res);
                          })
                          .name("JOIN:" + join->toString());

        for (size_t i = 0; i < candidates.size(); ++i) {
            node_ptr_t candidate = candidates[i];

            // 子流任务：真正执行 candidate 的逻辑
            auto task_do = candidate->type() == NodeType::FUNC
                               ? buildFuncTask(flowLike, candidate, frame)
                                     .name("BRCH_CAND_FUNC:" + candidate->toString())
                           : candidate->type() == NodeType::CALL
                               ? buildCallTask(flowLike, candidate, frame)
                                     .name("BRCH_CAND_CALL:" + candidate->toString())
                           : candidate->type() == NodeType::OPER
                               ? buildOperTask(flowLike, candidate, frame)
                                     .name("BRCH_CAND_OPER:" + candidate->toString())
                               : tf::Task(); // 其他类型不支持

            // 条件任务：仅为“保留 return 0 语义”，正常结束判断语句
            auto task_cond = flowLike.emplace([]() { return 0; })
                                 .name("BRCH_CAND_RET0:" + candidate->toString());

            // 连接：selector -> candidate_do -> candidate_cond -> joiner
            selector.precede(task_do);
            task_do.precede(task_cond);
            task_cond.precede(joiner);

            for (node_ptr_t in_n : candidate->ctrlInputs()) {
                if (in_n->type() == NodeType::BRCH || &in_n->graph() != graph) {
                    continue;
                }
                auto it = taskMap.find(in_n.get());
                if (it != taskMap.end()) {
                    task_do.succeed(it->second);
                }
            }
        }

        taskMap[brch.get()] = selector;
        taskMap[join.get()] = joiner;
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::buildNormalNodeTasks(
    FlowT &flowLike, Graph *graph, const frame_ptr_t &frame,
    std::unordered_map<Node *, tf::Task> &taskMap) {
    std::unordered_set<Node *> &skipNodes = globalBuildCtx_.skipNodes;
    for (const auto &n : graph->nodes()) {
        if (skipNodes.count(n.get()))
            continue;
        tf::Task t;
        switch (n->type()) {
        case NodeType::EXIT:
            t = buildExitTask(flowLike, n, frame);
            break;
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
        default:
            ASSERT(false, "Unsupported node type.");
        }
        taskMap[n.get()] = t;
    }

    // 端口节点（不包含在 nodes() 中，特殊处理）
    for (const auto &port : graph->ports()) {
        if (skipNodes.count(port.get()))
            continue;
        if (taskMap.find(port.get()) != taskMap.end())
            continue;
        auto t = buildPortTask(flowLike, port, frame);
        taskMap[port.get()] = t;
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::connectDependencies(
    FlowT &flow, Graph *graph, std::unordered_map<Node *, tf::Task> &taskMap) {
    std::unordered_set<Node *> &skipNodes = globalBuildCtx_.skipNodes;

    // 注意保留JOIN的输出和BRCH的输入
    auto add_edges_from_inputs = [&](const node_vec_t &inputs, tf::Task tsk) {
        for (const auto &in : inputs) {
            if (&in->graph() != graph)
                continue;
            if (skipNodes.count(in.get()) && in->type() != NodeType::JOIN)
                continue;

            auto it = taskMap.find(in.get());
            if (it != taskMap.end())
                it->second.precede(tsk);
        }
    };

    for (const auto &n : graph->nodes()) {
        if (skipNodes.count(n.get()) && n->type() != NodeType::BRCH)
            continue;
        auto it = taskMap.find(n.get());
        if (it == taskMap.end())
            continue;
        auto tsk = it->second;
        add_edges_from_inputs(n->dataInputs(), tsk);
        add_edges_from_inputs(n->ctrlInputs(), tsk);
    }
}

void TaskflowExecSchedPass::mark_map(const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    if (node->withInputs().size() != 1 || node->normInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<map>", 1, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = frame->get(node->withInputs().front());
    auto funcData = frame->get(node->normInputs().front());

    if (funcData->type()->code() != TypeCode::Function) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<map>", "Function", funcData->type()->toString());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidNormParameter, "Incorrect args.");
    }

    auto func = funcData->as<FunctionData>(Type::Func());
    type_ptr_t funcRetType = func->funcType()->exitType();

    auto spawn_unary = [&](data_ptr_t arg, data_ptr_t &out_slot) {
        sf.emplace([&, arg](tf::Subflow &isf) {
              auto &fg = func->graph();
              auto fframe = Frame::create(frame, fg);
              const auto &ports = fg.ports();
              ASSERT(ports.size() == 1, "Function should have exactly one parameter for map.");
              fframe->set(ports[0], arg);

              instantiate_graph_instance_generic(isf, &fg, fframe);
              isf.join();

              out_slot = get_graph_return(&fg, fframe);
          }).name("MAP_ELEM");
    };

    switch (targetData->type()->code()) {
    case TypeCode::Array: {
        auto arr = tt::as_shared<ArrayData>(targetData);
        const auto &elements = arr->raw();
        const size_t n = elements.size();
        data_vec_t results(n);
        for (size_t i = 0; i < n; ++i) {
            spawn_unary(elements[i], results[i]);
        }
        sf.join();
        frame->set(node, ArrayData::from(Type::Array(funcRetType), std::move(results)));
        break;
    }
    case TypeCode::Struct: {
        auto dict = tt::as_shared<StructData>(targetData);
        const auto &raw = dict->raw();
        std::vector<std::pair<std::string, data_ptr_t>> items;
        items.reserve(raw.size());
        for (const auto &kv : raw)
            items.emplace_back(kv.first, kv.second);

        std::vector<data_ptr_t> values(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            spawn_unary(items[i].second, values[i]);
        }
        sf.join();

        std::unordered_map<std::string, data_ptr_t> res;
        res.reserve(items.size());
        for (size_t i = 0; i < items.size(); ++i)
            res.emplace(items[i].first, values[i]);
        frame->set(node, StructData::create(std::move(res)));
        break;
    }
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<map>", "List/Array/Tuple/Vector/Struct", targetData->type()->toString());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }
}

void TaskflowExecSchedPass::mark_apply(const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    if (node->withInputs().size() != 1 || node->normInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<apply>", 1, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = frame->get(node->withInputs().front());
    auto funcData = frame->get(node->normInputs().front());
    ASSERT(funcData->type()->code() == TypeCode::Function, "apply expects a function.");
    auto func = funcData->as<FunctionData>(Type::Func());

    auto spawn_unary = [&](data_ptr_t arg, data_ptr_t &out_slot) {
        sf.emplace([&, arg](tf::Subflow &isf) {
              auto &fg = func->graph();
              auto fframe = Frame::create(frame, fg);
              const auto &ports = fg.ports();
              ASSERT(ports.size() == 1, "apply expects unary function.");
              fframe->set(ports[0], arg);

              instantiate_graph_instance_generic(isf, &fg, fframe);
              isf.join();

              out_slot = get_graph_return(&fg, fframe);
          }).name("APPLY_ELEM");
    };

    auto par_apply = [&](const data_vec_t &elements, auto createOut) {
        data_vec_t results(elements.size());
        for (size_t i = 0; i < elements.size(); ++i) {
            spawn_unary(elements[i], results[i]);
        }
        sf.join();
        frame->set(node, createOut(std::move(results)));
    };

    switch (targetData->type()->code()) {
    case TypeCode::Array: {
        auto arr = tt::as_shared<ArrayData>(targetData);
        par_apply(arr->raw(), [&](data_vec_t v) {
            return ArrayData::from(arr->type(), std::move(v));
        });
        break;
    }
    case TypeCode::Tuple: {
        auto tup = tt::as_shared<TupleData>(targetData);
        par_apply(tup->raw(), [&](data_vec_t v) {
            return TupleData::create(tup->type(), std::move(v));
        });
        break;
    }
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<apply>", "List/Array/Tuple/Vector", targetData->toString());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidWithParameter,
            "Unsupported type.");
    }
}

void TaskflowExecSchedPass::mark_filter(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    if (node->withInputs().size() != 1 || node->normInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<filter>", 1, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = frame->get(node->withInputs().front());
    auto funcData = frame->get(node->normInputs().front());
    ASSERT(funcData->type()->code() == TypeCode::Function, "filter expects a function.");
    auto func = funcData->as<FunctionData>(Type::Func());

    auto par_filter = [&](const data_vec_t &elements, auto createOut, auto elemType) {
        const size_t n = elements.size();
        std::vector<bool> keep(n, false);

        // 在 keep 定义之后再定义 spawn_pred，使其按引用捕获 keep
        auto spawn_pred = [&](data_ptr_t arg, size_t idx) {
            sf.emplace([&, arg, idx](tf::Subflow &isf) {
                  auto &fg = func->graph();
                  auto fframe = Frame::create(frame, fg);
                  const auto &ports = fg.ports();
                  ASSERT(ports.size() == 1, "filter expects unary predicate.");
                  fframe->set(ports[0], arg);

                  instantiate_graph_instance_generic(isf, &fg, fframe);
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
        frame->set(node, createOut(elemType, std::move(out)));
    };

    switch (targetData->type()->code()) {
    case TypeCode::Array: {
        auto arr = tt::as_shared<ArrayData>(targetData);
        par_filter(
            arr->raw(),
            [](auto t, data_vec_t v) { return ArrayData::from(t, std::move(v)); },
            arr->type());
        break;
    }
    case TypeCode::Tuple: {
        auto tup = tt::as_shared<TupleData>(targetData);
        par_filter(
            tup->raw(),
            [](auto t, data_vec_t v) { return TupleData::create(t, std::move(v)); },
            tup->type());
        break;
    }
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<filter>", "List/Array/Tuple/Vector", targetData->toString());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidWithParameter,
            "Unsupported type.");
    }
}

void TaskflowExecSchedPass::mark_reduce(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    if (node->withInputs().size() != 1 || node->normInputs().size() != 2) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<reduce>", 2, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = frame->get(node->withInputs().front());
    auto funcData = frame->get(node->normInputs()[0]);
    auto initData = frame->get(node->normInputs()[1]);
    ASSERT(funcData->type()->code() == TypeCode::Function, "reduce expects a function.");
    auto func = funcData->as<FunctionData>(Type::Func());

    data_vec_t elements;
    switch (targetData->type()->code()) {
    case TypeCode::Array:
        elements = tt::as_shared<ArrayData>(targetData)->raw();
        break;
    case TypeCode::Tuple:
        elements = tt::as_shared<TupleData>(targetData)->raw();
        break;
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<reduce>", "List/Array/Vector/Tuple", targetData->type()->toString());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidWithParameter,
            "Unsupported input type.");
    }

    if (elements.empty()) {
        frame->set(node, initData);
        return;
    }

    // 顺序左折叠，但每一步用 subflow 执行函数图；通过任务链保证顺序
    data_ptr_t acc = initData;
    tf::Task prev;
    bool has_prev = false;
    for (size_t i = 0; i < elements.size(); ++i) {
        auto step =
            sf.emplace([&, i](tf::Subflow &isf) {
                  auto &fg = func->graph();
                  auto fframe = Frame::create(frame, fg);
                  const auto &ports = fg.ports();
                  ASSERT(ports.size() == 2, "Binary function should have exactly two parameters.");
                  fframe->set(ports[0], acc);
                  fframe->set(ports[1], elements[i]);

                  instantiate_graph_instance_generic(isf, &fg, fframe);
                  isf.join();

                  acc = get_graph_return(&fg, fframe);
              }).name("REDUCE_STEP");
        if (has_prev)
            step.succeed(prev);
        prev = step;
        has_prev = true;
    }
    sf.join();
    frame->set(node, acc);
}

void TaskflowExecSchedPass::mark_foreach(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    if (node->withInputs().size() != 1 || node->normInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<foreach>", 1, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = frame->get(node->withInputs().front());
    auto funcData = frame->get(node->normInputs().front());
    ASSERT(funcData->type()->code() == TypeCode::Function, "foreach expects a function.");
    auto func = funcData->as<FunctionData>(Type::Func());

    auto spawn_unary_void = [&](data_ptr_t arg) {
        sf.emplace([&, arg](tf::Subflow &isf) {
              auto &fg = func->graph();
              auto fframe = Frame::create(frame, fg);
              const auto &ports = fg.ports();
              ASSERT(ports.size() == 1, "foreach expects unary function.");
              fframe->set(ports[0], arg);

              instantiate_graph_instance_generic(isf, &fg, fframe);
              isf.join();

              (void)get_graph_return(&fg, fframe);
          }).name("FOREACH_ELEM");
    };

    switch (targetData->type()->code()) {
    case TypeCode::Array: {
        auto elems = tt::as_shared<ArrayData>(targetData)->raw();
        for (auto &e : elems)
            spawn_unary_void(e);
        sf.join();
        frame->set(node, Data::null());
        break;
    }
    case TypeCode::Tuple: {
        auto elems = tt::as_shared<TupleData>(targetData)->raw();
        for (auto &e : elems)
            spawn_unary_void(e);
        sf.join();
        frame->set(node, Data::null());
        break;
    }
    case TypeCode::Struct: {
        auto dict = tt::as_shared<StructData>(targetData);
        for (const auto &kv : dict->raw())
            spawn_unary_void(kv.second);
        sf.join();
        frame->set(node, Data::null());
        break;
    }
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<foreach>", "List/Array/Tuple/Vector/Struct", targetData->toString());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidWithParameter,
            "Unsupported type.");
    }
}
