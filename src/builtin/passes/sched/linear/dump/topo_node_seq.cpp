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
 * Updated: Sep. 05, 2025
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
using namespace GIR;

string TopoNodeSeqDumpPass::pointerToIdent(const void *ptr, const char *prefix) {
    uintptr_t ptrVal = reinterpret_cast<uintptr_t>(ptr);
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

any TopoNodeSeqDumpPass::apply(const graph_ptr_t &graph) {
    // 先对子图进行拓扑排序
    // 收集所有被依赖的子图
    unordered_set<graph_ptr_t> graphSet;
    const auto visit = [&](const graph_ptr_t &g) {
        auto visit_impl = [&](auto &&self, const graph_ptr_t &node) -> void {
            if (graphSet.find(node) == graphSet.end()) {
                graphSet.insert(node);
                for (const auto &dep : node->dependencies()) {
                    self(self, dep);
                }
            }
        };
        visit_impl(visit_impl, g);
    };
    visit(graph);

    ostringstream oss;

    // 对子图进行拓扑排序
    auto sortedGraphs = topoSort(
        graphSet.begin(),
        graphSet.end(),
        [](const auto &g) { return g->inDegree(); },
        [](const auto &g) {
            vector<graph_ptr_t> outs;
            for (const auto &dep : g->dependents()) {
                if (auto sp = dep.lock()) {
                    outs.push_back(sp);
                }
            }
            return outs;
        });

    // 依次打印节点序列
    for (const auto &g : sortedGraphs) {
        // 对节点进行拓扑排序
        auto sortedNodes = topoSort(
            g->nodes().begin(),
            g->nodes().end(),
            [](const auto &n) { return n->inDegree(); },
            [](const auto &n) {
                vector<node_ptr_t> outs;
                outs.insert(outs.end(), n->dataOutputs().begin(), n->dataOutputs().end());
                outs.insert(outs.end(), n->ctrlOutputs().begin(), n->ctrlOutputs().end());
                return outs;
            });
        // 打印函数签名（含参数信息）
        oss << "FUNC: " << g->name();
        for (const auto &[_, portNode, __] : g->ports()) {
            oss << ", " << pointerToIdent(portNode.get());
        }
        oss << "\n";
        // 打印子图节点信息
        for (const auto &n : sortedNodes) {
            string res;
            switch (n->type()) {
            case NodeType::Function: {
                func_ptr_t func = tt::as_shared<FunctionNode>(n)->func();
                string name = func->name().empty() ? func->graph()->name() : func->name();
                res = format("CALL: {}", name);
                for (const auto &inputNode : n->dataInputs()) {
                    res += format(", {}", pointerToIdent(inputNode.get()));
                }
                break;
            }
            case NodeType::Operator: {
                auto oper = tt::as_shared<OperatorNode>(n);
                string name = oper->oper()->name();
                res = format("CALL: <{}>", name);
                for (const auto &inputNode : n->dataInputs()) {
                    res += format(", {}", pointerToIdent(inputNode.get()));
                }
                break;
            }
            case NodeType::Select: {
                auto selectNode = tt::as_shared<SelectNode>(n);
                if (selectNode->selectType() == SelectNode::SelectType::Branch) {
                    const auto &ins = selectNode->dataInputs();
                    const auto &outs = selectNode->ctrlOutputs();
                    res = format(
                        "BRCH: {}? {}: {}",
                        pointerToIdent(ins[0].get()),
                        pointerToIdent(outs[0].get()),
                        pointerToIdent(outs[1].get()));
                } else {
                    const auto &ins = selectNode->ctrlInputs();
                    res = format(
                        "JOIN: {}, {}",
                        pointerToIdent(ins[0].get()),
                        pointerToIdent(ins[1].get()));
                }
                break;
            }
            default:
                res = format("NODE: {} ({})", n->data2str(), string(n->dataType()));
            }
            oss << "    [" << pointerToIdent(n.get()) << "] " << res << "\n";
        }
        // 打印返回节点
        oss << "RETN: " << pointerToIdent(g->output().get()) << "\n\n";
    }

    oss << format("CALL: {}", graph->name()) << "\n";

    return oss.str();
}