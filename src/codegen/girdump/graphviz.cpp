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
#include <iomanip>
#include <sstream>

using namespace std;
using namespace gir;

inline string pointerToHex(const void *ptr) {
    stringstream ss;
    ss << "0x" << hex << setw(sizeof(void *) * 2) << setfill('0') << reinterpret_cast<uintptr_t>(ptr);
    return ss.str();
}

void GraphVizPass::pushIndent() {
    indent_ += string(tabSize_, ' ');
    depth_++;
}

void GraphVizPass::popIndent() {
    indent_ = indent_.substr(0, indent_.size() - tabSize_);
    depth_--;
}

void GraphVizPass::reset() {}

void GraphVizPass::reset(context_ptr_t &context) { context_ = context; }

std::any GraphVizPass::apply(gir::graph_ptr_t &graph) {
    string res;
    if (depth_ == 0) {
        res += "digraph GraphIR {\r\n";
    } else {
        func_ptr_t func = graph->func();
        res += "subgraph cluster_" + pointerToHex(graph.get()) + " {\r\n";
        res += indent_ + "label=" + func->name() + ";\r\n";
    }
    size_t cnt = 0;
    pushIndent();
    for (const auto &node : graph->nodes()) {
        res += indent_;
        string label;
        switch (node->type()) {
        case NodeType::STRUCT:
            [[fallthrough]];
        case NodeType::DATA: {
            const auto &name = context_->getNodeIdent(node);
            if (name.has_value()) {
                label = name.value();
            } else {
                label = to_string(cnt++);
            }
            break;
        }
        case NodeType::FUNCTOR: {
            func_node_ptr_t func = func_node_ptr_cast(node);
            label = func->type()->name();
            graph_ptr_t subGraph = func->subGraph();
            res += any_cast<string>(apply(subGraph));
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
        res += pointerToHex(node.get()) + " [label=" + label + "];\r\n";
    }
    for (const auto &node : graph->nodes()) {
        const auto &vec = node->outputs();
        for (size_t i = 0; i < vec.size(); i++) {
            res += indent_ + pointerToHex(node.get()) + " -> " + pointerToHex(vec[i].get()) +
                   " [label=" + to_string(i) + "];\r\n";
        }
    }
    popIndent();
    res += "}\r\n";
    return res;
}
