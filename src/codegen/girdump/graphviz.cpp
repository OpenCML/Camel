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
 * Updated: Oct. 21, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "graphviz.h"
#include "utils/log.h"

using namespace std;
using namespace gir;

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

inline string pointerToIdent(const void *ptr) {
    stringstream ss;
    ss << "P" << hex << setw(8) << setfill('0') << reinterpret_cast<uintptr_t>(ptr);
    return ss.str();
}

std::any GraphVizPass::apply(gir::graph_ptr_t &graph) {
    string res = baseIndent_;
    unordered_map<size_t, pair<string, bool>> portsNameMap;
    void *ret;
    if (depth_ == 0) {
        res += "digraph GraphIR {\r\n";
    } else {
        func_ptr_t func = graph->func();
        func_type_ptr_t type = func->funcType();
        for (const auto &[idx, _, isVar] : graph->ports()) {
            portsNameMap[idx] = make_pair(type->nameAt(idx), isVar);
        }
        res += "subgraph cluster_" + pointerToIdent(graph.get()) + " {\r\n";
        res += baseIndent_ + indent_ + "label=\"" + func->name() + "\";\r\n";
    }
    size_t cnt = 0;
    node_vec_t &nodes = graph->nodes();
    for (size_t i = 0; i < nodes.size(); i++) {
        string label;
        const node_ptr_t &node = nodes[i];
        switch (node->type()) {
        case NodeType::STRUCT:
            [[fallthrough]];
        case NodeType::DATA: {
            if (portsNameMap.find(i) != portsNameMap.end()) {
                label = portsNameMap[i].first;
            } else {
                const auto &name = context_->getNodeIdent(node);
                if (name.has_value()) {
                    label = name.value();
                } else {
                    label = to_string(cnt++);
                }
            }
            break;
        }
        case NodeType::FUNCTOR: {
            func_node_ptr_t func = func_node_ptr_cast(node);
            label = func->type()->name();
            graph_ptr_t subGraph = func->subGraph();
            res += baseIndent_;
            pushIndent();
            res += any_cast<string>(apply(subGraph));
            popIndent();
            break;
        }
        case NodeType::OPERATOR: {
            op_node_ptr_t op = op_node_ptr_cast(node);
            label = op->opName();
            break;
        }
        default:
            throw runtime_error("Unknown node type");
        }
        res += baseIndent_ + indent_ + pointerToIdent(node.get()) + " [label=\"" + label + "\"];\r\n";
    }
    for (const auto &node : graph->nodes()) {
        const auto &vec = node->inputs();
        for (size_t i = 0; i < vec.size(); i++) {
            if (vec[i] == nullptr) {
                continue;
            }
            res += baseIndent_ + indent_ + pointerToIdent(vec[i].get()) + " -> " + pointerToIdent(node.get()) +
                   " [label=\"" + to_string(i) + "\"];\r\n";
        }
    }
    res += baseIndent_ + "}\r\n";
    return res;
}
