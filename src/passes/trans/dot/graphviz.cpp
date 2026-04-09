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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "graphviz.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/scope.h"
#include "camel/utils/type.h"

#include <format>
#include <iomanip>
#include <ranges>
#include <sstream>

using namespace std;
using namespace GIR;
using namespace camel::core::context;

namespace {

const camel::runtime::GCNode *
lookupRecord(camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t index) {
    return graph ? graph->node(index) : nullptr;
}

string graphTooltip(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Graph tooltip requires a runtime graph.");
    auto *funcType = graph->funcType();
    return funcType ? funcType->toString() : string("<unknown>");
}

string recordTooltip(camel::runtime::GCGraph *graph, const camel::runtime::GCNode &record) {
    ASSERT(graph != nullptr, "Node tooltip requires a runtime graph.");
    return std::format("{}::node#{}", graph->name(), record.dataIndex);
}

string portLabel(const camel::runtime::GCNode &record) {
    (void)record;
    return "PORT";
}

string recordLabel(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
    const camel::runtime::GCNode &record) {
    switch (record.kind) {
    case camel::runtime::GCNodeKind::Data:
        return "DATA";
    case camel::runtime::GCNodeKind::Port:
        return portLabel(record);
    case camel::runtime::GCNodeKind::Cast:
        return "CAST";
    case camel::runtime::GCNodeKind::Copy:
        return "COPY";
    case camel::runtime::GCNodeKind::Fill:
        return "FILL";
    case camel::runtime::GCNodeKind::Accs: {
        const auto *body = graph->nodeBodyAs<camel::runtime::GCAccsBody>(nodeRef);
        if (body->accsKind == camel::runtime::GCAccsKind::StructKey) {
            return "." + std::string(body->key());
        }
        return std::format(".{}", body->value);
    }
    case camel::runtime::GCNodeKind::Brch:
        return "BRCH";
    case camel::runtime::GCNodeKind::Join:
        return "JOIN";
    case camel::runtime::GCNodeKind::Call:
        return "CALL";
    case camel::runtime::GCNodeKind::Bind:
        return "BIND";
    case camel::runtime::GCNodeKind::Func: {
        auto *callee = graph->directCalleeGraphOf(nodeRef);
        return callee ? callee->name() : "FUNC";
    }
    case camel::runtime::GCNodeKind::Oper:
        return std::string(graph->nodeBodyAs<camel::runtime::GCOperBody>(nodeRef)->uri());
    case camel::runtime::GCNodeKind::Sync:
        return "SYNC";
    case camel::runtime::GCNodeKind::Gate:
        return "GATE";
    case camel::runtime::GCNodeKind::Dref:
        return "DREF";
    default:
        return "NODE";
    }
}

string recordShape(const camel::runtime::GCNode &record) {
    switch (record.kind) {
    case camel::runtime::GCNodeKind::Cast:
    case camel::runtime::GCNodeKind::Copy:
    case camel::runtime::GCNodeKind::Fill:
    case camel::runtime::GCNodeKind::Accs:
    case camel::runtime::GCNodeKind::Brch:
    case camel::runtime::GCNodeKind::Join:
    case camel::runtime::GCNodeKind::Call:
    case camel::runtime::GCNodeKind::Bind:
    case camel::runtime::GCNodeKind::Oper:
    case camel::runtime::GCNodeKind::Sync:
    case camel::runtime::GCNodeKind::Gate:
        return "diamond";
    case camel::runtime::GCNodeKind::Func:
        return "Mdiamond";
    default:
        return "circle";
    }
}

string recordStyle(const camel::runtime::GCNode &record) {
    switch (record.kind) {
    case camel::runtime::GCNodeKind::Sync:
    case camel::runtime::GCNodeKind::Gate:
        return "dashed";
    default:
        return "solid";
    }
}

string recordSize(const camel::runtime::GCNode &record) {
    return record.kind == camel::runtime::GCNodeKind::Func ? "width=1.1, height=1.1" : "";
}

} // namespace

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

    while (std::getline(stream, paragraph)) {
        size_t len = paragraph.length();

        if (len <= maxWidth) {
            lines.push_back(paragraph);
            continue;
        }

        if (len <= maxWidth * 2) {
            size_t splitPos = len / 2;
            size_t left     = paragraph.rfind(' ', splitPos);
            size_t right    = paragraph.find(' ', splitPos);

            if (left != std::string::npos && (splitPos - left) <= (right - splitPos)) {
                splitPos = left;
            } else if (right != std::string::npos) {
                splitPos = right;
            }

            std::string first  = paragraph.substr(0, splitPos);
            std::string second = paragraph.substr(splitPos);
            if (!second.empty() && second[0] == ' ') {
                second = second.substr(1);
            }
            lines.push_back(first);
            lines.push_back(second);
            continue;
        }

        size_t pos = 0;
        while (pos < len) {
            size_t take = std::min(maxWidth, len - pos);
            lines.push_back(paragraph.substr(pos, take));
            pos += take;
        }
    }

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

    int hexDigits  = 1;
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

std::string GraphVizDumpPass::dumpGraph(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "GraphViz dump requires a non-null runtime graph.");
    ASSERT(graph->hasNodePayload(), "Runtime graph dump requires materialized node payload.");
    if (visitedGraphs_.find(graph) != visitedGraphs_.end()) {
        return string("");
    }
    visitedGraphs_.insert(graph);

    auto nodeIdent = [&](const camel::runtime::GCNode *record) { return pointerToIdent(record); };

    string funcId   = pointerToIdent(graph, "F");
    string exitId   = pointerToIdent(graph, "O");
    string funcName = graph->name();
    string res;

    res += baseIndent_;

    if (depth_ == 0) {
        res += std::format(
            "digraph GraphIR {{\r\n"
            "    graph [rankdir=LR, fontsize=18];\r\n"
            "    node [fixedsize=true, width=1, height=1, fontsize=18];\r\n"
            "    edge [minlen=2];\r\n");
    } else {
        res += std::format("subgraph cluster_{} {{\r\n", funcId);
        res += std::format("{}{}label=\"{}\";\r\n", baseIndent_, indent_, funcName);
        res += std::format("{}{}tooltip=\"{}\";\r\n", baseIndent_, indent_, graphTooltip(graph));
    }

    graph->traceGraphs([&](camel::runtime::GCGraph *dep) {
        if (!dep) {
            return;
        }
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
            "GraphViz",
            "Dumping reachable graph '{}' of graph '{}'",
            dep->name(),
            graph->name()));
        pushIndent();
        res += dumpGraph(dep);
        popIndent();
    });

    if (!graph->isRoot() && (!graph->normPorts().empty() || !graph->withPorts().empty())) {
        res += std::format(
            "{}{}{} [label=\"ARGS\", style=dashed, shape=circle];\r\n",
            baseIndent_,
            indent_,
            funcId);
    }

    res += std::format(
        "{}{}{} [label=\"EXIT\", style=dashed, shape=circle];\r\n",
        baseIndent_,
        indent_,
        exitId);

    for (uint32_t portIndex : graph->withPorts()) {
        const auto *port = lookupRecord(graph, portIndex);
        ASSERT(port != nullptr, "Runtime with-port record is missing.");
        string label   = portLabel(*port);
        string tooltip = recordTooltip(graph, *port);
        res += std::format(
            "{}{}{} [label=\"{}\", shape=circle, style=solid, tooltip=\"{}\"];\r\n",
            baseIndent_,
            indent_,
            nodeIdent(port),
            escape(wrapText(label, 7, 2)),
            std::format("{}\\n{}", escape(label), escape(tooltip)));
    }
    for (uint32_t portIndex : graph->normPorts()) {
        const auto *port = lookupRecord(graph, portIndex);
        ASSERT(port != nullptr, "Runtime norm-port record is missing.");
        string label   = portLabel(*port);
        string tooltip = recordTooltip(graph, *port);
        res += std::format(
            "{}{}{} [label=\"{}\", shape=circle, style=solid, tooltip=\"{}\"];\r\n",
            baseIndent_,
            indent_,
            nodeIdent(port),
            escape(wrapText(label, 7, 2)),
            std::format("{}\\n{}", escape(label), escape(tooltip)));
    }
    for (uint32_t closureIndex : graph->closureNodes()) {
        const auto *port = lookupRecord(graph, closureIndex);
        ASSERT(port != nullptr, "Runtime closure-port record is missing.");
        string label   = portLabel(*port);
        string tooltip = recordTooltip(graph, *port);
        res += std::format(
            "{}{}{} [label=\"{}\", shape=circle, style=dashed, tooltip=\"{}\"];\r\n",
            baseIndent_,
            indent_,
            nodeIdent(port),
            escape(wrapText(label, 7, 2)),
            std::format("{}\\n{}", escape(label), escape(tooltip)));
    }

    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const auto nodeRef = it.ref();
        const auto *node   = *it;
        if (!node || node->kind == camel::runtime::GCNodeKind::Port ||
            node->kind == camel::runtime::GCNodeKind::Dref) {
            continue;
        }
        string label   = recordLabel(graph, nodeRef, *node);
        string tooltip = recordTooltip(graph, *node);
        string shape   = recordShape(*node);
        string style   = recordStyle(*node);
        string size    = recordSize(*node);
        res += std::format(
            "{}{}{} [label=\"{}\", shape={}, style={}{}, tooltip=\"{}\"];\r\n",
            baseIndent_,
            indent_,
            nodeIdent(node),
            escape(wrapText(label, 7, 2)),
            shape,
            style,
            size.empty() ? "" : ", " + size,
            std::format("{}\\n{}", escape(label), escape(tooltip)));
    }

    for (size_t i = 0; i < graph->withPorts().size(); ++i) {
        const auto *port = lookupRecord(graph, graph->withPorts()[i]);
        ASSERT(port != nullptr, "Runtime with-port record is missing.");
        res += std::format(
            "{}{}{} -> {} [label=\"{}\", style=dashed, arrowhead=empty];\r\n",
            baseIndent_,
            indent_,
            funcId,
            nodeIdent(port),
            i);
    }
    for (size_t i = 0; i < graph->normPorts().size(); ++i) {
        const auto *port = lookupRecord(graph, graph->normPorts()[i]);
        ASSERT(port != nullptr, "Runtime norm-port record is missing.");
        res += std::format(
            "{}{}{} -> {} [label=\"{}\", style=solid];\r\n",
            baseIndent_,
            indent_,
            funcId,
            nodeIdent(port),
            i);
    }

    const auto *exitRecord = graph->exitNode();
    ASSERT(exitRecord != nullptr, "Runtime graph exit node is missing.");
    res += std::format(
        "{}{}{} -> {} [label=\"{}\", style=solid];\r\n",
        baseIndent_,
        indent_,
        nodeIdent(exitRecord),
        exitId,
        0);

    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const auto nodeIndex = it.ref();
        const auto *node     = *it;
        ASSERT(node != nullptr, "Runtime node record lookup failed.");

        for (size_t i = 0; i < graph->withInputsOf(nodeIndex).size(); ++i) {
            auto inputIndex   = graph->withInputsOf(nodeIndex)[i];
            const auto *input = lookupRecord(graph, inputIndex);
            ASSERT(input != nullptr, "Runtime with-input record is missing.");
            size_t outIdx = 0;
            for (auto outIndex : graph->withOutputsOf(inputIndex)) {
                if (outIndex == nodeIndex) {
                    break;
                }
                outIdx++;
            }
            res += std::format(
                "{}{}{} -> {} [label=\"{}|{}\", style=dashed];\r\n",
                baseIndent_,
                indent_,
                nodeIdent(input),
                nodeIdent(node),
                outIdx,
                i);
        }

        for (size_t i = 0; i < graph->normInputsOf(nodeIndex).size(); ++i) {
            auto inputIndex   = graph->normInputsOf(nodeIndex)[i];
            const auto *input = lookupRecord(graph, inputIndex);
            ASSERT(input != nullptr, "Runtime norm-input record is missing.");
            size_t outIdx = 0;
            for (auto outIndex : graph->normOutputsOf(inputIndex)) {
                if (outIndex == nodeIndex) {
                    break;
                }
                outIdx++;
            }
            res += std::format(
                "{}{}{} -> {} [label=\"{}|{}\"];\r\n",
                baseIndent_,
                indent_,
                nodeIdent(input),
                nodeIdent(node),
                outIdx,
                i);
        }

        for (size_t i = 0; i < graph->ctrlInputsOf(nodeIndex).size(); ++i) {
            auto inputIndex   = graph->ctrlInputsOf(nodeIndex)[i];
            const auto *input = lookupRecord(graph, inputIndex);
            ASSERT(input != nullptr, "Runtime ctrl-input record is missing.");
            size_t outIdx = 0;
            for (auto outIndex : graph->ctrlOutputsOf(inputIndex)) {
                if (outIndex == nodeIndex) {
                    break;
                }
                outIdx++;
            }
            res += std::format(
                "{}{}{} -> {} [label=\"{}|{}\", style=dashed, arrowhead=empty];\r\n",
                baseIndent_,
                indent_,
                nodeIdent(input),
                nodeIdent(node),
                outIdx,
                i);
        }
    }

    res += baseIndent_ + "}\r\n";
    return res;
}

GraphVizDumpPass::GraphVizDumpPass(const context_ptr_t &context)
    : RuntimeGraphTranslatePass(context) {}

graph_ptr_t GraphVizDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    os << dumpGraph(graph);
    return GIR::Graph::null();
}
