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
#include "utils/scope.h"
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
    std::istringstream stream(text);
    std::string line;

    // Step 1: Split input by existing '\n'
    while (std::getline(stream, line)) {
        size_t pos = 0;
        while (pos < line.length()) {
            size_t take = std::min(maxWidth, line.length() - pos);
            lines.push_back(line.substr(pos, take));
            pos += take;
        }
    }

    // Step 2: Handle maxLines limit
    if (lines.size() > maxLines) {
        std::vector<std::string> limitedLines;
        for (size_t i = 0; i < maxLines; ++i) {
            if (i == maxLines - 1) {
                // Last line: truncate and add ellipsis if needed
                std::string lastLine = lines[i];
                if (lastLine.length() > maxWidth) {
                    lastLine = lastLine.substr(0, maxWidth);
                }
                if (lastLine.length() > 3) {
                    lastLine = lastLine.substr(0, maxWidth - 3) + "...";
                } else {
                    lastLine = std::string(maxWidth, '.');
                }
                limitedLines.push_back(lastLine);
            } else {
                limitedLines.push_back(lines[i]);
            }
        }
        lines = std::move(limitedLines);
    }

    // Step 3: Join lines with '\n'
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i != lines.size() - 1) {
            result += '\n';
        }
    }

    return result;
}

string GraphVizDumpPass::pointerToIdent(const void *ptr, const char *prefix) {
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

void GraphVizDumpPass::pushIndent() {
    baseIndent_ += indent_;
    depth_++;
}

void GraphVizDumpPass::popIndent() {
    baseIndent_ = baseIndent_.substr(0, baseIndent_.size() - indent_.size());
    depth_--;
}

any GraphVizDumpPass::apply(const GIR::graph_ptr_t &graph) {
    if (visitedGraphs_.find(graph) != visitedGraphs_.end()) {
        // Already visited, skip to avoid duplication
        return string("");
    } else {
        visitedGraphs_.insert(graph);
    }

    string funcId = pointerToIdent(graph.get(), "F");
    string exitId = pointerToIdent(graph->arena().get(), "R");
    string funcName = graph->name();
    string res;
    unordered_map<size_t, pair<string, bool>> portsNameMap;
    void *retNodePtr = graph->output().get();

    res += baseIndent_;
    if (depth_ == 0) {
        res += string("digraph GraphIR {\r\n") +
               string("    graph [rankdir=LR, fontsize=18];\r\n") +
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

    for (auto &[_, subGraph] : graph->subGraphs()) {
        l.in("GraphViz")
            .debug("Dumping subgraph '{}' of graph '{}'", subGraph->name(), graph->name());
        pushIndent();
        res += any_cast<string>(apply(subGraph));
        popIndent();
    }
    for (const auto &dep : graph->dependencies()) {
        l.in("GraphViz")
            .debug("Dumping dependency graph '{}' of graph '{}'", dep->name(), graph->name());
        pushIndent();
        res += any_cast<string>(apply(dep));
        popIndent();
    }

    vector<node_ptr_t> argNodes;
    if (!graph->isRoot() && graph->ports().size() > 0) {
        res +=
            baseIndent_ + indent_ + funcId + " [label=\"ARGS\", style=dashed, shape=circle];\r\n";
    }
    const node_vec_t &nodes = graph->nodes();
    for (size_t i = 0; i < nodes.size(); i++) {
        string label;
        const node_ptr_t &node = nodes[i];
        string shape = "circle";
        string style = "solid";
        string size = "";
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
        case NodeType::Access: {
            auto accessNode = tt::as_shared<AccessNode>(node);
            label = "$" + accessNode->index2String();
            break;
        }
        case NodeType::Struct: {
            auto structNode = tt::as_shared<StructNode>(node);
            label = structNode->dataType()->toString();
            shape = "box";
            size = "width=1, height=0.5";
            break;
        }
        case NodeType::Source: {
            if (portsNameMap.find(i) != portsNameMap.end()) {
                label = portsNameMap[i].first;
                style = "dashed";
                argNodes.push_back(node);
            } else {
                if (node->type() == NodeType::Source) {
                    data_ptr_t data = node->eval(graph->arena());
                    label = data->toString();
                }
            }
            break;
        }
        case NodeType::Function: {
            func_ptr_t func = tt::as_shared<FunctionNode>(node)->func();
            label = func->name().empty() ? func->graph()->name() : func->name();
            shape = "Mdiamond";
            size = "width=1.1, height=1.1";
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
        res += baseIndent_ + indent_ + pointerToIdent(node.get()) + " [label=\"" +
               escape(wrapText(label, 8, 2)) + "\", shape=" + shape + ", style=" + style +
               (size.empty() ? ("") : (", " + size)) + "];\r\n";
    }
    if (!graph->isRoot()) {
        res += baseIndent_ + indent_ + exitId +
               " [label=\"RETN\", shape=doublecircle, width=0.9, height=0.9];\r\n";
    }

    for (const auto &node : argNodes) {
        res += baseIndent_ + indent_ + funcId + " -> " + pointerToIdent(node.get()) +
               " [style=dashed, arrowhead=empty];\r\n";
    }
    for (const auto &node : graph->nodes()) {
        auto vec = node->withInputs();
        for (size_t i = 0; i < vec.size(); i++) {
            if (vec[i] == nullptr) {
                continue;
            }
            res += baseIndent_ + indent_ + pointerToIdent(vec[i].get()) + " -> " +
                   pointerToIdent(node.get()) + " [label=\"" + to_string(i) +
                   "\", style=dashed];\r\n";
        }
        vec = node->normInputs();
        for (size_t i = 0; i < vec.size(); i++) {
            if (vec[i] == nullptr) {
                continue;
            }
            res += baseIndent_ + indent_ + pointerToIdent(vec[i].get()) + " -> " +
                   pointerToIdent(node.get()) + " [label=\"" + to_string(i) + "\"];\r\n";
        }
        vec = node->ctrlInputs();
        for (size_t i = 0; i < vec.size(); i++) {
            if (vec[i] == nullptr) {
                continue;
            }
            res += baseIndent_ + indent_ + pointerToIdent(vec[i].get()) + " -> " +
                   pointerToIdent(node.get()) + " [style=dashed, arrowhead=empty];\r\n";
        }
        if (node.get() == retNodePtr) {
            res += baseIndent_ + indent_ + pointerToIdent(node.get()) + " -> " + exitId + ";\r\n";
        }
    }
    if (graph->nodes().empty() && graph->output() != nullptr) {
        // Sometimes there are no nodes in the graph, but there are
        // output nodes captured by closures
        res += baseIndent_ + indent_ + pointerToIdent(graph->output().get()) + " -> " + exitId +
               ";\r\n";
    }

    res += baseIndent_ + "}\r\n";
    return res;
}
