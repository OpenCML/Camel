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
 * Created: Sep. 05, 2025
 * Updated: Oct. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "topo_node_seq.h"
#include "builtin/algo/topo.h"
#include "utils/log.h"
#include "utils/type.h"

#include <iomanip>
#include <queue>
#include <sstream>

using namespace std;
using namespace GraphIR;

TopoNodeSeqDumpPass::TopoNodeSeqDumpPass(const context_ptr_t &ctx) : GraphTranslatePass(ctx) {}

string TopoNodeSeqDumpPass::pointerToIdent(const void *ptr, const char *prefix) {
    return getPtrRepr(prefix, reinterpret_cast<uintptr_t>(ptr), showRawPtr);
}

string TopoNodeSeqDumpPass::getPtrRepr(const string &prefix, uintptr_t ptrVal, bool showRawPtr) {
    string prefixStr(prefix);
    if (!showRawPtr) {
        auto &mapForPrefix = ptrsMap_[prefixStr];
        if (mapForPrefix.find(ptrVal) == mapForPrefix.end()) {
            mapForPrefix[ptrVal] = ptrCnt_[prefixStr]++;
        }
        ptrVal = mapForPrefix[ptrVal];
    }

    int hexDigits = 1;
    uintptr_t temp = ptrVal;
    while (temp >>= 4) {
        ++hexDigits;
    }

    int width = ((hexDigits + 1) / 2) * 2;

    stringstream ss;
    ss << prefix << hex << uppercase << setw(width) << setfill('0') << ptrVal << dec << nouppercase;
    return ss.str();
}

graph_ptr_t TopoNodeSeqDumpPass::apply(graph_ptr_t &graph, std::ostream &os) {
    auto optMainGraph = graph->getSubGraphsByName("main");
    ASSERT(optMainGraph.has_value(), "Main graph not found.");
    auto mainGraphSet = optMainGraph.value();
    ASSERT(!mainGraphSet.empty(), "Main graph set is empty.");
    ASSERT(mainGraphSet.size() == 1, "Multiple main graphs found.");
    auto mainGraph = *mainGraphSet.begin();

    // 收集所有被依赖的子图
    auto sortedGraphs =
        findReachable(mainGraph, [](const graph_ptr_t &g) { return g->dependencies(); });

    ostringstream oss;

    // 依次打印节点序列
    for (const auto &g : sortedGraphs) {
        // 对节点进行拓扑排序
        node_ptr_t retNode = g->exitNode();
        auto sortedNodes = findReachable(
            retNode,
            [](const node_ptr_t &n) {
                vector<node_ptr_t> ins;
                ins.reserve(n->dataInputs().size() + n->ctrlInputs().size());
                for (const auto &in : n->ctrlInputs()) {
                    if (&in->graph() == &n->graph()) // only consider nodes in the same graph
                        ins.push_back(in);
                }
                for (const auto &in : n->dataInputs()) {
                    if (&in->graph() == &n->graph()) // only consider nodes in the same graph
                        ins.push_back(in);
                }
                return ins;
            },
            true);
        EXEC_WHEN_DEBUG([&]() {
            l.in("Topo").debug("Topologically sorted nodes for graph {}:", graph->name());
            for (const auto &n : sortedNodes) {
                l.in("Topo").debug("  {}", n->toString());
            }
            if (sortedNodes.size() != graph->nodes().size() - 1) {
                GraphIR::node_vec_t unreachableNodes;
                for (const auto &n : graph->nodes()) {
                    if (n != retNode &&
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
                l.in("Topo").warn(
                    "Unreachable nodes in graph {} detected: {}",
                    graph->name(),
                    nodeStrs);
            }
        }());
        // 打印函数签名（含参数信息）
        oss << "FUNC: " << g->name();
        for (const auto &portNode : g->ports()) {
            oss << ", " << pointerToIdent(portNode.get());
        }
        oss << "\n";
        // 打印子图节点信息
        for (const auto &n : sortedNodes) {
            string res;
            switch (n->type()) {
            case NodeType::FUNC: {
                func_ptr_t func = tt::as_shared<FuncNode>(n)->func();
                string name = func->name().empty() ? func->graph().name() : func->name();
                res = format("CALL: {}", name);
                for (const auto &inputNode : n->dataInputs()) {
                    res += format(", {}", pointerToIdent(inputNode.get()));
                }
                break;
            }
            case NodeType::OPER: {
                auto oper = tt::as_shared<OperNode>(n);
                string name = oper->oper()->name();
                res = format("CALL: <{}>", name);
                for (const auto &inputNode : n->dataInputs()) {
                    res += format(", {}", pointerToIdent(inputNode.get()));
                }
                break;
            }
            case NodeType::BRCH: {
                auto brchNode = tt::as_shared<BrchNode>(n);
                const auto &ins = brchNode->dataInputs();
                const auto &outs = brchNode->ctrlOutputs();
                res = format(
                    "BRCH: {}? {}: {}",
                    pointerToIdent(ins[0].get()),
                    pointerToIdent(outs[0].get()),
                    pointerToIdent(outs[1].get()));
                break;
            }
            case NodeType::JOIN: {
                auto joinNode = tt::as_shared<JoinNode>(n);
                const auto &ins = joinNode->ctrlInputs();
                res = format(
                    "JOIN: {}, {}",
                    pointerToIdent(ins[0].get()),
                    pointerToIdent(ins[1].get()));
                break;
            }
            default:
                res = format("NODE: {}", n->toString());
            }
            oss << "    [" << pointerToIdent(n.get()) << "] " << res << "\n";
        }
        // 打印返回节点
        oss << "RETN: " << pointerToIdent(g->exitNode().get()) << "\n\n";
    }

    oss << format("CALL: {}", graph->name()) << "\n";

    os << oss.str();

    return graph;
}
