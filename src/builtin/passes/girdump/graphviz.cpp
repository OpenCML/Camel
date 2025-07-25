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
 * Created: Oct. 21, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "graphviz.h"
#include "utils/log.h"
#include "utils/type.h"

using namespace std;
using namespace GIR;

string GraphVizDumpPass::pointerToIdent(const void *ptr) {
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

void GraphVizDumpPass::pushIndent() {
    baseIndent_ += indent_;
    depth_++;
}

void GraphVizDumpPass::popIndent() {
    baseIndent_ = baseIndent_.substr(0, baseIndent_.size() - indent_.size());
    depth_--;
}

void GraphVizDumpPass::reset() {}

void GraphVizDumpPass::reset(context_ptr_t &context) { context_ = context; }

any GraphVizDumpPass::apply(GIR::graph_ptr_t &graph) {
    string funcId = pointerToIdent(graph.get());
    string res;
    unordered_map<size_t, pair<string, bool>> portsNameMap;
    void *retNodePtr = graph->output().get();

    res += baseIndent_;
    if (depth_ == 0) {
        res += "digraph GraphIR {\r\n";
    } else {
        func_type_ptr_t type = graph->funcType();
        for (size_t i = 0; i < graph->ports().size(); i++) {
            const string name = type->argNameAt(i);
            bool isVar = type->variableMap().at(name);
            portsNameMap[i] = make_pair(name, isVar);
        }
        res += "subgraph cluster_" + funcId + " {\r\n";
        string funcName = graph->name().empty() ? lambdaFuncIdents_[graph] : graph->name();
        res += baseIndent_ + indent_ + "label=\"" + funcName + "\";\r\n";
    }

    size_t lambdaFuncCnt = 0;
    for (auto &subGraph : graph->subGraphs()) {
        pushIndent();
        if (subGraph->name().empty()) {
            lambdaFuncIdents_[subGraph] = "__lambda_" + to_string(lambdaFuncCnt++) + "__";
        }
        res += any_cast<string>(apply(subGraph));
        popIndent();
    }

    size_t dataCnt = 0;
    const node_vec_t &nodes = graph->nodes();
    for (size_t i = 0; i < nodes.size(); i++) {
        string label;
        string shape;
        const node_ptr_t &node = nodes[i];
        switch (node->type()) {
        case NodeType::Select:
            [[fallthrough]];
        case NodeType::Access:
            [[fallthrough]];
        case NodeType::Struct:
            [[fallthrough]];
        case NodeType::Source: {
            if (portsNameMap.find(i) != portsNameMap.end()) {
                label = portsNameMap[i].first;
                shape = "circle";
            } else {
                const auto &name = nodeIdents_.find(node);
                if (name != nodeIdents_.end()) {
                    label = name->second;
                } else {
                    label = "__N" + to_string(dataCnt++) + "__";
                }
                shape = "cylinder";
            }
            break;
        }
        case NodeType::Function: {
            func_ptr_t func = tt::as_shared<FunctionNode>(node)->func();
            label = func->name().empty() ? lambdaFuncIdents_[func->graph()] : func->name();
            shape = "parallelogram";
            break;
        }
        case NodeType::Operator: {
            auto oper = tt::as_shared<OperatorNode>(node);
            label = oper->oper()->name();
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
        const auto &vec = node->normInputs();
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
