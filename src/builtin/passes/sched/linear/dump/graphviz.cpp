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
 * Updated: Sep. 28, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "graphviz.h"
#include "utils/scope.h"
#include "utils/type.h"

using namespace std;
using namespace GraphIR;

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
    std::string paragraph;

    // Step 1: Split input by existing '\n'
    while (std::getline(stream, paragraph)) {
        size_t len = paragraph.length();

        // If the line fits in one line, push directly
        if (len <= maxWidth) {
            lines.push_back(paragraph);
            continue;
        }

        // If the line fits in two lines but not one, try to split in the middle
        if (len <= maxWidth * 2) {
            size_t splitPos = len / 2;

            // Try to split at whitespace near the middle
            size_t left = paragraph.rfind(' ', splitPos);
            size_t right = paragraph.find(' ', splitPos);

            if (left != std::string::npos && (splitPos - left) <= (right - splitPos)) {
                splitPos = left;
            } else if (right != std::string::npos) {
                splitPos = right;
            }

            std::string first = paragraph.substr(0, splitPos);
            std::string second = paragraph.substr(splitPos);
            // Trim leading space of second line
            if (!second.empty() && second[0] == ' ')
                second = second.substr(1);
            lines.push_back(first);
            lines.push_back(second);
            continue;
        }

        // Otherwise, break into fixed maxWidth chunks
        size_t pos = 0;
        while (pos < len) {
            size_t take = std::min(maxWidth, len - pos);
            lines.push_back(paragraph.substr(pos, take));
            pos += take;
        }
    }

    // Step 2: Handle maxLines limit
    if (lines.size() > maxLines) {
        std::vector<std::string> limitedLines;
        for (size_t i = 0; i < maxLines; ++i) {
            if (i == maxLines - 1) {
                std::string lastLine = lines[i];
                if (lastLine.length() > maxWidth) {
                    lastLine = lastLine.substr(0, maxWidth);
                }
                if (maxWidth >= 3 && lastLine.length() > 3) {
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

any GraphVizDumpPass::apply(const graph_ptr_t &graph) {
    if (visitedGraphs_.find(graph) != visitedGraphs_.end()) {
        // Skip if the graph has already been visited to avoid duplication
        return string("");
    } else {
        visitedGraphs_.insert(graph);
    }

    string funcId = pointerToIdent(graph.get(), "F");
    string funcName = graph->name();
    string res;
    unordered_map<size_t, pair<string, bool>> portsNameMap;

    res += baseIndent_;

    if (depth_ == 0) {
        res += std::format("digraph GraphIR {{\r\n"
                           "    graph [rankdir=LR, fontsize=18];\r\n"
                           "    node [fixedsize=true, width=1, height=1, fontsize=18];\r\n"
                           "    edge [minlen=2];\r\n");
    } else {
        // Non-root graph: collect port names and types
        func_type_ptr_t type = graph->funcType();
        for (size_t i = 0; i < graph->portNodes().size(); i++) {
            const string name = type->argNameAt(i);
            bool isVar = type->variableMap().at(name);
            portsNameMap[i] = make_pair(name, isVar);
        }

        res += std::format("subgraph cluster_{} {{\r\n", funcId);
        res += std::format("{}{}label=\"{}\";\r\n", baseIndent_, indent_, funcName);
    }

    // Recursively dump subgraphs first
    for (auto &[_, subGraph] : graph->subGraphs()) {
        EXEC_WHEN_DEBUG(
            l.in("GraphViz")
                .debug("Dumping subgraph '{}' of graph '{}'", subGraph->name(), graph->name()));
        pushIndent();
        res += any_cast<string>(apply(subGraph));
        popIndent();
    }

    // Dump dependency graphs if any
    for (const auto &dep : graph->dependencies()) {
        EXEC_WHEN_DEBUG(
            l.in("GraphViz")
                .debug("Dumping dependency graph '{}' of graph '{}'", dep->name(), graph->name()));
        pushIndent();
        res += any_cast<string>(apply(dep));
        popIndent();
    }

    // Draw ARGS node to represent function arguments
    if (!graph->isRoot() && !graph->portNodes().empty()) {
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
        tooltip = graph->name() + "::" + node->toString();

        switch (node->type()) {
        case NodeType::DATA: {
            auto sourceNode = tt::as_shared<DataNode>(node);
            data_ptr_t data = sourceNode->dataOf(*graph->arena());
            label = data->toString();
            break;
        }
        case NodeType::PORT: {
            ASSERT(portsNameMap.contains(i), "Port index out of range.");
            label = portsNameMap[i].first;
            style = "dashed";
            break;
        }
        case NodeType::COPY: {
            label = "COPY";
            shape = "diamond";
            break;
        }
        case NodeType::FILL: {
            label = "FILL";
            shape = "diamond";
            break;
        }
        case NodeType::ACCS: {
            auto accessNode = tt::as_shared<AccsNode>(node);
            label = "$" + accessNode->index2String();
            shape = "diamond";
            break;
        }
        case NodeType::BRCH: {
            label = "BRCH";
            shape = "diamond";
            break;
        }
        case NodeType::JOIN: {
            label = "JOIN";
            shape = "diamond";
            break;
        }
        case NodeType::CALL: {
            label = "CALL";
            shape = "diamond";
            break;
        }
        case NodeType::WITH: {
            label = "WITH";
            shape = "diamond";
            break;
        }
        case NodeType::FUNC: {
            func_ptr_t func = tt::as_shared<FuncNode>(node)->func();
            label = func->name().empty() ? func->graph()->name() : func->name();
            shape = "Mdiamond";
            size = "width=1.1, height=1.1";
            break;
        }
        case NodeType::OPER: {
            auto oper = tt::as_shared<OperNode>(node);
            label = oper->oper()->name();
            shape = "diamond";
            break;
        }
        case NodeType::EXIT: {
            label = "RETN";
            shape = "doublecircle";
            size = "width=0.9, height=0.9";
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

    // Connect ARGS node to port nodes
    size_t withIdx = 0, normIdx = 0;
    for (const auto &[portNode, isWithArg] : graph->portNodes()) {
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
    }

    res += baseIndent_ + "}\r\n";
    return res;
}
