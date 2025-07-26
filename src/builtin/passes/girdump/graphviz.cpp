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

std::string escape(const std::string &input) {
    std::string result;
    for (char c : input) {
        if (c == '"') {
            result += "\\\"";
        } else if (c == '\n') {
            result += "\\n";
        } else if (c == '\r') {
            result += "\\r";
        } else if (c == '\\') {
            result += "\\\\";
        } else if (c == '\t') {
            result += "\\t";
        } else if (c < 32 || c > 126) {
            result += "\\x" + to_string(static_cast<unsigned char>(c));
        } else {
            result += c;
        }
    }
    return result;
}

std::string wrapText(const std::string &text, size_t maxWidth, size_t maxLines) {
    std::vector<std::string> lines;
    size_t len = text.length();
    size_t maxTotalChars = maxWidth * maxLines;

    // Case 1: Text fits in a single line
    if (len <= maxWidth) {
        return text;
    }

    // Case 2: Text fits within maxLines * maxWidth, distribute evenly
    if (len <= maxTotalChars) {
        size_t baseLen = len / maxLines;
        size_t extra = len % maxLines;
        size_t pos = 0;

        for (size_t i = 0; i < maxLines; ++i) {
            size_t take = baseLen + (i < extra ? 1 : 0);
            if (take > 0) {
                lines.push_back(text.substr(pos, take));
                pos += take;
            } else {
                lines.push_back(""); // Fill empty line if needed
            }
        }
    }
    // Case 3: Text too long, truncate and add ellipsis
    else {
        size_t pos = 0;
        for (size_t line = 0; line < maxLines; ++line) {
            size_t remaining = len - pos;
            size_t take = std::min(maxWidth, remaining);

            if (line == maxLines - 1 && remaining > maxWidth) {
                // Add ellipsis if there's more text left
                if (maxWidth > 3) {
                    lines.push_back(text.substr(pos, maxWidth - 3) + "...");
                } else {
                    lines.push_back(std::string(maxWidth, '.')); // Fallback if width too small
                }
                break;
            } else {
                lines.push_back(text.substr(pos, take));
                pos += take;
            }
        }
    }

    // Join lines with newline character
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i != lines.size() - 1) {
            result += '\n';
        }
    }

    return result;
}

string GraphVizDumpPass::pointerToIdent(const void *ptr) {
    uintptr_t ptrVal = reinterpret_cast<uintptr_t>(ptr);
    if (!showRawPtr) {
        if (ptrsMap_.find(ptrVal) == ptrsMap_.end()) {
            ptrsMap_[ptrVal] = ptrCnt++;
        }
        ptrVal = ptrsMap_[ptrVal];
    }

    int hexDigits = 1;
    uintptr_t temp = ptrVal;
    while (temp >>= 4) {
        ++hexDigits;
    }

    int width = ((hexDigits + 1) / 2) * 2;

    stringstream ss;
    ss << "P" << hex << uppercase << setw(width) << setfill('0') << ptrVal << dec << nouppercase;
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
    string exitId = pointerToIdent(graph->arena().get());
    string funcName = graph->name().empty() ? lambdaFuncIdents_[graph] : graph->name();
    string res;
    unordered_map<size_t, pair<string, bool>> portsNameMap;
    void *retNodePtr = graph->output().get();

    res += baseIndent_;
    if (depth_ == 0) {
        res += string("digraph GraphIR {\r\n") + string("    graph [rankdir=LR, fontsize=18];\r\n") +
               string("    node [fixedsize=true, width=1, height=1, fontsize=18];\r\n") +
               string("    edge [minlen=2];\r\n");
    } else {
        func_type_ptr_t type = graph->funcType();
        for (size_t i = 0; i < graph->ports().size(); i++) {
            const string name = type->argNameAt(i);
            bool isVar = type->variableMap().at(name);
            portsNameMap[i] = make_pair(name, isVar);
        }
        res += "subgraph cluster_" + funcId + " {\r\n";
        res += baseIndent_ + indent_ + "label=\"" + funcName + "\";\r\n";
    }

    size_t lambdaFuncCnt = 0;
    for (auto &subGraph : graph->subGraphs()) {
        pushIndent();
        if (subGraph->name().empty()) {
            lambdaFuncIdents_[subGraph] = "__F" + to_string(lambdaFuncCnt++) + "__";
        }
        res += any_cast<string>(apply(subGraph));
        popIndent();
    }

    vector<node_ptr_t> argNodes;
    if (!graph->isRoot()) {
        res += baseIndent_ + indent_ + funcId + " [label=\"ARGS\", style=dashed, shape=circle];\r\n";
    }
    size_t dataCnt = 0;
    const node_vec_t &nodes = graph->nodes();
    for (size_t i = 0; i < nodes.size(); i++) {
        string label;
        string shape = "circle";
        string style = "solid";
        const node_ptr_t &node = nodes[i];
        switch (node->type()) {
        case NodeType::Select: {
            auto selectNode = tt::as_shared<SelectNode>(node);
            if (selectNode->selectType() == SelectNode::SelectType::Branch) {
                label = "BRCH";
            } else {
                label = "JOIN";
            }
            shape = "diamond";
            break;
        }
        case NodeType::Access:
            [[fallthrough]];
        case NodeType::Struct:
            [[fallthrough]];
        case NodeType::Source: {
            if (portsNameMap.find(i) != portsNameMap.end()) {
                label = portsNameMap[i].first;
                style = "dashed";
                argNodes.push_back(node);
            } else {
                if (node->type() == NodeType::Source) {
                    data_ptr_t data = node->eval(graph->arena());
                    label = data->toString();
                } else {
                    const auto &name = nodeIdents_.find(node);
                    if (name != nodeIdents_.end()) {
                        label = name->second;
                    } else {
                        label = "__N" + to_string(dataCnt++) + "__";
                    }
                }
            }
            break;
        }
        case NodeType::Function: {
            func_ptr_t func = tt::as_shared<FunctionNode>(node)->func();
            label = func->name().empty() ? lambdaFuncIdents_[func->graph()] : func->name();
            shape = "diamond";
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
        res += baseIndent_ + indent_ + pointerToIdent(node.get()) + " [label=\"" + escape(wrapText(label, 8, 2)) +
               "\", shape=" + shape + ", style=" + style + "];\r\n";
    }
    if (!graph->isRoot()) {
        res += baseIndent_ + indent_ + exitId + " [label=\"RETN\", shape=doublecircle, width=0.9, height=0.9];\r\n";
    }

    for (const auto &node : argNodes) {
        res += baseIndent_ + indent_ + funcId + " -> " + pointerToIdent(node.get()) +
               " [style=dashed, arrowhead=empty];\r\n";
    }
    for (const auto &node : graph->nodes()) {
        auto vec = node->normInputs();
        for (size_t i = 0; i < vec.size(); i++) {
            if (vec[i] == nullptr) {
                continue;
            }
            res += baseIndent_ + indent_ + pointerToIdent(vec[i].get()) + " -> " + pointerToIdent(node.get()) +
                   " [label=\"" + to_string(i) + "\"];\r\n";
        }
        vec = node->withInputs();
        for (size_t i = 0; i < vec.size(); i++) {
            if (vec[i] == nullptr) {
                continue;
            }
            res += baseIndent_ + indent_ + pointerToIdent(vec[i].get()) + " -> " + pointerToIdent(node.get()) +
                   " [label=\"" + to_string(i) + "\", style=dashed];\r\n";
        }
        vec = node->ctrlInputs();
        for (size_t i = 0; i < vec.size(); i++) {
            if (vec[i] == nullptr) {
                continue;
            }
            res += baseIndent_ + indent_ + pointerToIdent(vec[i].get()) + " -> " + pointerToIdent(node.get()) +
                   " [label=\"" + to_string(i) + "\", style=dashed, arrowhead=empty];\r\n";
        }
        if (node.get() == retNodePtr) {
            res += baseIndent_ + indent_ + pointerToIdent(node.get()) + " -> " + exitId + ";\r\n";
        }
    }

    res += baseIndent_ + "}\r\n";
    return res;
}
