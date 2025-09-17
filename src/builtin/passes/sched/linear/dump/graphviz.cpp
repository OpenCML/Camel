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
        // Skip if the graph has already been visited to avoid duplication
        return string("");
    } else {
        visitedGraphs_.insert(graph);
    }

    string funcId = pointerToIdent(graph.get(), "F");
    string exitId = pointerToIdent(graph->arena().get(), "R");
    string funcName = graph->name();
    string res;
    unordered_map<size_t, pair<string, bool>> portsNameMap;
    void *retNodePtr = graph->hasOutput() ? graph->output().get() : nullptr;

    res += baseIndent_;

    if (depth_ == 0) {
        res += std::format(
            "digraph GraphIR {{\r\n"
            "    graph [rankdir=LR, fontsize=18];\r\n"
            "    node [fixedsize=true, width=1, height=1, fontsize=18];\r\n"
            "    edge [minlen=2];\r\n");
    } else {
        // Non-root graph: collect port names and types
        func_type_ptr_t type = graph->funcType();
        for (size_t i = 0; i < graph->ports().size(); i++) {
            const string name = type->argNameAt(i);
            bool isVar = type->variableMap().at(name);
            portsNameMap[i] = make_pair(name, isVar);
        }

        res += std::format("subgraph cluster_{} {{\r\n", funcId);
        res += std::format("{}{}label=\"{}\";\r\n", baseIndent_, indent_, funcName);
    }

    // Recursively dump subgraphs first
    for (auto &[_, subGraph] : graph->subGraphs()) {
        l.in("GraphViz")
            .debug("Dumping subgraph '{}' of graph '{}'", subGraph->name(), graph->name());
        pushIndent();
        res += any_cast<string>(apply(subGraph));
        popIndent();
    }

    // Dump dependency graphs if any
    for (const auto &dep : graph->dependencies()) {
        l.in("GraphViz")
            .debug("Dumping dependency graph '{}' of graph '{}'", dep->name(), graph->name());
        pushIndent();
        res += any_cast<string>(apply(dep));
        popIndent();
    }

    // Draw ARGS node to represent function arguments
    if (!graph->isRoot() && !graph->ports().empty()) {
        res += std::format(
            "{}{}{} [label=\"ARGS\", style=dashed, shape=circle];\r\n",
            baseIndent_,
            indent_,
            funcId);
    }

    // Draw nodes inside the graph
    const node_vec_t &nodes = graph->nodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto &node = nodes[i];
        string label, tooltip, shape = "circle", style = "solid", size;
        tooltip = node->toString();

        switch (node->type()) {
        case NodeType::Select: {
            auto selectNode = tt::as_shared<SelectNode>(node);
            label = selectNode->selectType() == SelectNode::SelectType::Branch ? "BRCH" : "JOIN";
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
            if (portsNameMap.contains(i)) {
                label = portsNameMap[i].first;
                style = "dashed";
            } else {
                auto sourceNode = tt::as_shared<SourceNode>(node);
                data_ptr_t data = sourceNode->dataOf(*graph->arena());
                label = data->toString();
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
            throw runtime_error("Unknown node type encountered during GraphViz generation.");
        }

        res += std::format(
            "{}{}{} [label=\"{}\", shape={}, style={}{}, tooltip=\"{}\"];\r\n",
            baseIndent_,
            indent_,
            pointerToIdent(node.get()),
            escape(wrapText(label, 7, 2)),
            shape,
            style,
            size.empty() ? "" : ", " + size,
            std::format("{}\\n{}", escape(label), escape(tooltip)));
    }

    // Draw return node if not root
    if (!graph->isRoot()) {
        res += std::format(
            "{}{}{} [label=\"RETN\", shape=doublecircle, width=0.9, height=0.9];\r\n",
            baseIndent_,
            indent_,
            exitId);
    }

    // Connect ARGS node to port nodes
    size_t withIdx = 0, normIdx = 0;
    for (const auto &[portNode, isWithArg] : graph->ports()) {
        string style = isWithArg ? "dashed, arrowhead=empty" : "solid";
        res += std::format(
            "{}{}{} -> {} [label=\"{}\", style={}];\r\n",
            baseIndent_,
            indent_,
            funcId,
            pointerToIdent(portNode.get()),
            isWithArg ? withIdx++ : normIdx++,
            style);
    }

    // Connect nodes via input edges
    for (const auto &node : graph->nodes()) {
        auto withInputs = node->withInputs();
        for (size_t i = 0; i < withInputs.size(); ++i) {
            if (withInputs[i]) {
                size_t outIdx = 0;
                for (const auto &out : withInputs[i]->dataOutputs()) {
                    if (out == node) {
                        break;
                    }
                    outIdx++;
                }
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", style=dashed];\r\n",
                    baseIndent_,
                    indent_,
                    pointerToIdent(withInputs[i].get()),
                    pointerToIdent(node.get()),
                    outIdx,
                    i);
            }
        }

        auto normInputs = node->normInputs();
        for (size_t i = 0; i < normInputs.size(); ++i) {
            if (normInputs[i]) {
                size_t outIdx = 0;
                for (const auto &out : normInputs[i]->dataOutputs()) {
                    if (out == node) {
                        break;
                    }
                    outIdx++;
                }
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\"];\r\n",
                    baseIndent_,
                    indent_,
                    pointerToIdent(normInputs[i].get()),
                    pointerToIdent(node.get()),
                    outIdx,
                    i);
            }
        }

        auto ctrlInputs = node->ctrlInputs();
        for (size_t i = 0; i < ctrlInputs.size(); ++i) {
            if (ctrlInputs[i]) {
                size_t outIdx = 0;
                for (const auto &out : ctrlInputs[i]->ctrlOutputs()) {
                    if (out == node) {
                        break;
                    }
                    outIdx++;
                }
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", style=dashed, arrowhead=empty];\r\n",
                    baseIndent_,
                    indent_,
                    pointerToIdent(ctrlInputs[i].get()),
                    pointerToIdent(node.get()),
                    outIdx,
                    i);
            }
        }

        // Connect return node
        if (node.get() == retNodePtr) {
            res += std::format(
                "{}{}{} -> {};\r\n",
                baseIndent_,
                indent_,
                pointerToIdent(node.get()),
                exitId);
        }
    }

    // Special case: graph has no nodes but has output set (outer entity capture)
    if (graph->nodes().empty() && graph->hasOutput()) {
        res += std::format(
            "{}{}{} -> {};\r\n",
            baseIndent_,
            indent_,
            pointerToIdent(graph->output().get()),
            exitId);
    }

    res += baseIndent_ + "}\r\n";
    return res;
}