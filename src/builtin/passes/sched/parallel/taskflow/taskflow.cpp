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
 * Updated: Oct. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "taskflow.h"
#include "core/data/composed/array.h"
#include "core/data/primary.h"

#include <fstream>
#include <queue>
#include <regex>
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
        .name("DATA:" + regex_replace(n->toString(), regex("\""), "\\\""));
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
        .emplace([n, frame]() {
            data_ptr_t source = frame->get(n->dataInputs().front());
            auto acc = tt::as_shared<AccsNode>(n);
            if (acc->isNum()) {
                size_t idx = acc->index<size_t>();
                auto a = tt::as_shared<ArrayData>(source);
                ASSERT(idx < a->raw().size(), "Array index out of bounds.");
                frame->set(n, a->raw()[idx]);
            } else {
                std::string key = acc->index<std::string>();
                auto d = tt::as_shared<StructData>(source);
                ASSERT(d->raw().contains(key), "Struct key not found.");
                frame->set(n, d->raw().at(key));
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

            auto layer = flowLike.emplace([]() {}).name("BRCH_CAND_LAYER");

            // 子流任务：真正执行 candidate 的逻辑
            auto task_do = tf::Task();
            if (candidate->type() == NodeType::FUNC)
                task_do = buildFuncTask(flowLike, candidate, frame)
                              .name("BRCH_CAND_FUNC:" + candidate->toString());
            else if (candidate->type() == NodeType::CALL)
                task_do = buildCallTask(flowLike, candidate, frame)
                              .name("BRCH_CAND_CALL:" + candidate->toString());
            else if (candidate->type() == NodeType::OPER)
                task_do = buildOperTask(flowLike, candidate, frame)
                              .name("BRCH_CAND_OPER:" + candidate->toString());
            else
                ASSERT(false, "Unsupported candidate node type in BRCH-JOIN.");

            // 条件任务：return 0 确保之后的节点可以在其他条件未被执行的情况下执行
            // 额外建一个任务是因为有 subflow 的 task 不能有返回值
            auto task_cond =
                flowLike.emplace([]() { return 0; }).name("BRCH_CAND_RET:" + candidate->toString());

            // 连接：selector -> layer -> candidate_do -> candidate_cond -> joiner
            selector.precede(layer);
            layer.precede(task_do);
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

void TaskflowExecSchedPass::mark_map_arr(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front());
    auto funcData = frame->get(node->withInputs().front());

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

    auto arr = tt::as_shared<ArrayData>(targetData);
    const auto &elements = arr->raw();
    const size_t n = elements.size();
    data_vec_t results(n);
    for (size_t i = 0; i < n; ++i) {
        spawn_unary(elements[i], results[i]);
    }
    sf.join();
    frame->set(node, ArrayData::from(Type::Array(funcRetType), std::move(results)));
}

void TaskflowExecSchedPass::mark_apply_arr(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front());
    auto funcData = frame->get(node->withInputs().front());
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

    auto arr = tt::as_shared<ArrayData>(targetData);
    par_apply(arr->raw(), [&](data_vec_t v) { return ArrayData::from(arr->type(), std::move(v)); });
}

void TaskflowExecSchedPass::mark_filter_arr(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front());
    auto funcData = frame->get(node->withInputs().front());
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

    auto arr = tt::as_shared<ArrayData>(targetData);
    par_filter(
        arr->raw(),
        [](auto t, data_vec_t v) { return ArrayData::from(t, std::move(v)); },
        arr->type());
}

void TaskflowExecSchedPass::mark_reduce_arr(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front());
    auto funcData = frame->get(node->withInputs()[0]);
    auto initData = frame->get(node->withInputs()[1]);
    auto func = funcData->as<FunctionData>(Type::Func());

    data_vec_t elements = tt::as_shared<ArrayData>(targetData)->raw();

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

void TaskflowExecSchedPass::mark_unordered_reduce_arr(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front());
    auto funcData = frame->get(node->withInputs()[0]);
    auto initData = frame->get(node->withInputs()[1]);
    auto func = funcData->as<FunctionData>(Type::Func());

    data_vec_t elements = tt::as_shared<ArrayData>(targetData)->raw();

    if (elements.empty()) {
        frame->set(node, initData);
        return;
    }

    constexpr size_t THRESH = 100;

    // 分治：构建任务图计算 [l, r) 的折叠结果到 out
    auto reduce_dc =
        [&](auto &&self, size_t l, size_t r, std::shared_ptr<data_ptr_t> out) -> tf::Task {
        const size_t n = r - l;
        if (n <= THRESH) {
            // 叶子：在线性顺序上在当前子流中逐步计算
            return sf
                .emplace([&, l, r, out](tf::Subflow &isf) {
                    data_ptr_t res = elements[l];
                    tf::Task prev;
                    bool has_prev = false;
                    for (size_t i = l + 1; i < r; ++i) {
                        auto step =
                            isf.emplace([&, i](tf::Subflow &lsf) {
                                   auto &fg = func->graph();
                                   auto fframe = Frame::create(frame, fg);
                                   const auto &ports = fg.ports();
                                   ASSERT(
                                       ports.size() == 2,
                                       "Binary function should have exactly two parameters.");
                                   fframe->set(ports[0], res);
                                   fframe->set(ports[1], elements[i]);

                                   instantiate_graph_instance_generic(lsf, &fg, fframe);
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

            auto tc = sf.emplace([&, left_out, right_out, out](tf::Subflow &isf) {
                            // 合并两个子区间：保持 left 在前、right 在后
                            auto &fg = func->graph();
                            auto fframe = Frame::create(frame, fg);
                            const auto &ports = fg.ports();
                            ASSERT(
                                ports.size() == 2,
                                "Binary function should have exactly two parameters.");
                            fframe->set(ports[0], *left_out);
                            fframe->set(ports[1], *right_out);

                            instantiate_graph_instance_generic(isf, &fg, fframe);
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
        sf.emplace([&, total](tf::Subflow &isf) {
              auto &fg = func->graph();
              auto fframe = Frame::create(frame, fg);
              const auto &ports = fg.ports();
              ASSERT(ports.size() == 2, "Binary function should have exactly two parameters.");
              fframe->set(ports[0], initData);
              fframe->set(ports[1], *total);

              instantiate_graph_instance_generic(isf, &fg, fframe);
              isf.join();

              auto res = get_graph_return(&fg, fframe);
              frame->set(node, res);
          }).name("REDUCE_FINAL");
    final.succeed(root);
    sf.join();
}

void TaskflowExecSchedPass::mark_foreach_arr(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front());
    auto funcData = frame->get(node->withInputs().front());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto add_step = [&](const data_ptr_t &arg, tf::Task &prev, bool &has_prev) {
        auto step = sf.emplace([&, arg](tf::Subflow &isf) {
                          auto &fg = func->graph();
                          auto fframe = Frame::create(frame, fg);
                          const auto &ports = fg.ports();
                          ASSERT(ports.size() == 1, "foreach expects unary function.");
                          fframe->set(ports[0], arg);

                          instantiate_graph_instance_generic(isf, &fg, fframe);
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
    frame->set(node, Data::null());
}

void TaskflowExecSchedPass::mark_unordered_foreach_arr(
    const node_ptr_t &node, frame_ptr_t frame, tf::Subflow &sf) {
    auto targetData = frame->get(node->normInputs().front());
    auto funcData = frame->get(node->withInputs().front());
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

    auto elems = tt::as_shared<ArrayData>(targetData)->raw();
    for (auto &e : elems)
        spawn_unary_void(e);
    sf.join();
    frame->set(node, Data::null());
}
