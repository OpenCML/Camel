/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: Oct. 21, 2024
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "graphviz.h"
#include "utils/log.h"

using namespace std;
using namespace gir;

string GraphVizPass::pointerToIdent(const void *ptr) {
    uintptr_t ptrVal = reinterpret_cast<uintptr_t>(ptr);
    if (!showRawPtr) {
        if (ptrsMap_.find(ptrVal) == ptrsMap_.end()) {
            ptrsMap_[ptrVal] = ptrCnt++;
        }
        ptrVal = ptrsMap_[ptrVal];
    }
    stringstream ss;
    ss << "P" << hex << uppercase << setw(6) << setfill('0') << ptrVal << dec << nouppercase;
    return ss.str();
}

void GraphVizPass::pushIndent() {
    baseIndent_ += indent_;
    depth_++;
}

void GraphVizPass::popIndent() {
    baseIndent_ = baseIndent_.substr(0, baseIndent_.size() - indent_.size());
    depth_--;
}

void GraphVizPass::reset() {}

void GraphVizPass::reset(context_ptr_t &context) { context_ = context; }

any GraphVizPass::apply(gir::graph_ptr_t &graph) {
    string funcId = pointerToIdent(graph.get());
    string res;
    unordered_map<size_t, pair<string, bool>> portsNameMap;
    void *retNodePtr = graph->output().get();

    res += baseIndent_;
    if (depth_ == 0) {
        res += "digraph GraphIR {\r\n";
    } else {
        func_type_ptr_t type = graph->funcType();
        for (const auto &[idx, _, isVar] : graph->ports()) {
            portsNameMap[idx] = make_pair(type->argNameAt(idx), isVar);
        }
        res += "subgraph cluster_" + funcId + " {\r\n";
        string funcName = type->name().empty() ? lambdaFuncIdents_[type] : type->name();
        res += baseIndent_ + indent_ + "label=\"" + funcName + "\";\r\n";
    }

    size_t lambdaFuncCnt = 0;
    for (auto &subGraph : graph->subGraphs()) {
        pushIndent();
        func_type_ptr_t type = subGraph->funcType();
        if (type->name().empty()) {
            lambdaFuncIdents_[type] = "__lambda_" + to_string(lambdaFuncCnt++) + "__";
        }
        res += any_cast<string>(apply(subGraph));
        popIndent();
    }

    size_t dataCnt = 0;
    node_vec_t &nodes = graph->nodes();
    for (size_t i = 0; i < nodes.size(); i++) {
        string label;
        string shape;
        const node_ptr_t &node = nodes[i];
        switch (node->type()) {
        case NodeType::STRUCT:
            [[fallthrough]];
        case NodeType::DATA: {
            if (portsNameMap.find(i) != portsNameMap.end()) {
                label = portsNameMap[i].first;
                shape = "circle";
            } else {
                const auto &name = context_->getNodeIdent(node);
                if (name.has_value()) {
                    label = name.value();
                } else {
                    label = "__D" + to_string(dataCnt++) + "__";
                }
                shape = "cylinder";
            }
            break;
        }
        case NodeType::FUNCTOR: {
            func_ptr_t func = func_node_ptr_cast(node)->func();
            func_type_ptr_t type = func->funcType();
            label = type->name().empty() ? lambdaFuncIdents_[type] : type->name();
            shape = "parallelogram";
            break;
        }
        case NodeType::OPERATOR: {
            oper_node_ptr_t oper = oper_node_ptr_cast(node);
            label = oper->operName();
            shape = "diamond";
            break;
        }
        default:
            throw runtime_error("Unknown node type");
        }
        res +=
            baseIndent_ + indent_ + pointerToIdent(node.get()) + " [label=\"" + label + "\", shape=" + shape + "];\r\n";
    }
    res += baseIndent_ + indent_ + funcId + " [label=\"RET\", shape=doublecircle];\r\n";

    for (const auto &node : graph->nodes()) {
        const auto &vec = node->inputs();
        for (size_t i = 0; i < vec.size(); i++) {
            if (vec[i] == nullptr) {
                continue;
            }
            res += baseIndent_ + indent_ + pointerToIdent(vec[i].get()) + " -> " + pointerToIdent(node.get()) +
                   " [label=\"" + to_string(i) + "\"];\r\n";
        }
        if (node.get() == retNodePtr) {
            res += baseIndent_ + indent_ + pointerToIdent(node.get()) + " -> " + funcId + ";\r\n";
        }
    }

    res += baseIndent_ + "}\r\n";
    return res;
}
