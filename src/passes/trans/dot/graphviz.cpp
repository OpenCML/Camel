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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "graphviz.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_semantics.h"
#include "camel/passes/trans/gir_metadata.h"

#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/source/ids.h"
#include "camel/core/source/manager.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/node_roles.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/scope.h"
#include "camel/utils/type.h"

#include <format>
#include <iomanip>
#include <ranges>
#include <sstream>

using namespace std;
using namespace camel::core::context;
using namespace camel::passes::gir;

namespace {

string recordShape(const camel::runtime::GCNode &record) {
    switch (record.kind) {
    case camel::runtime::GCNodeKind::Cast:
    case camel::runtime::GCNodeKind::Copy:
    case camel::runtime::GCNodeKind::Fill:
    case camel::runtime::GCNodeKind::Accs:
    case camel::runtime::GCNodeKind::Brch:
    case camel::runtime::GCNodeKind::Join:
    case camel::runtime::GCNodeKind::Call:
    case camel::runtime::GCNodeKind::Oper:
    case camel::runtime::GCNodeKind::Sync:
    case camel::runtime::GCNodeKind::Gate:
        return "diamond";
    case camel::runtime::GCNodeKind::Func:
    case camel::runtime::GCNodeKind::Loop:
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

std::string GraphVizDumpPass::debugOriginAttr(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t ref) {
    if (!graph) {
        return "";
    }
    const uint64_t origin = graph->nodeOrigin(ref);
    if (origin == 0) {
        return "";
    }
    if (!context_ || !context_->sourceContext()) {
        return std::format(", origin={}", origin);
    }
    auto *sc        = context_->sourceContext().get();
    const auto *rec = sc->origin(origin);
    if (!rec || rec->primarySpan == camel::source::kInvalidSpanId) {
        return std::format(", origin={}", origin);
    }
    auto range = sc->resolveOrigin(origin);
    return std::format(
        ", origin={}, span=\"{}:{}-{}:{}\", srcfile=\"{}\"",
        origin,
        range.start.line,
        range.start.character,
        range.end.line,
        range.end.character,
        escape(sc->pathForOrigin(origin)));
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
        if (!config_.rich) {
            res += "digraph GraphIR {\n";
        } else {
            res += std::format(
                "digraph GraphIR {{\n"
                "    graph [rankdir=LR, fontsize=18];\n"
                "    node [fixedsize=true, width=1, height=1, fontsize=18];\n"
                "    edge [minlen=2];\n");
        }
    } else {
        res += std::format("subgraph cluster_{} {{\n", funcId);
        res += std::format("{}{}label=\"{}\";\n", baseIndent_, indent_, funcName);
        if (config_.rich) {
            res += std::format("{}{}tooltip=\"{}\";\n", baseIndent_, indent_, graphTooltip(graph));
        }
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
        if (!config_.rich) {
            res += std::format(
                "{}{}{} [label=\"ARGS\", type=\"ARGS\"];\n",
                baseIndent_,
                indent_,
                funcId);
        } else {
            res += std::format(
                "{}{}{} [label=\"ARGS\", type=\"ARGS\", style=dashed, shape=circle];\n",
                baseIndent_,
                indent_,
                funcId);
        }
    }

    if (!config_.rich) {
        res +=
            std::format("{}{}{} [label=\"EXIT\", type=\"EXIT\"];\n", baseIndent_, indent_, exitId);
    } else {
        res += std::format(
            "{}{}{} [label=\"EXIT\", type=\"EXIT\", style=dashed, shape=circle];\n",
            baseIndent_,
            indent_,
            exitId);
    }

    auto emitPort = [&](camel::runtime::gc_node_ref_t ref, const char *nodeType) {
        const auto *port = lookupRecord(graph, ref);
        ASSERT(port != nullptr, "Runtime port record is missing.");
        const auto info    = portInfo(graph, ref);
        const string label = info.name.empty() ? "PORT" : info.name;
        string metadata;
        if (!info.name.empty())
            metadata += std::format(", param_name=\"{}\"", escape(info.name));
        if (!info.kind.empty())
            metadata +=
                std::format(", param_kind=\"{}\", param_index=\"{}\"", info.kind, info.index);
        if (port->dataType())
            metadata += std::format(", value_type=\"{}\"", escape(port->dataType()->toString()));
        metadata += debugOriginAttr(graph, ref);
        const string styling = !config_.rich
                                   ? ""
                                   : std::format(
                                         ", shape=circle, style={}, tooltip=\"{}\"",
                                         string(nodeType) == "CLOSURE_PORT" ? "dashed" : "solid",
                                         escape(label + "\n" + recordTooltip(graph, *port)));
        res += std::format(
            "{}{}{} [label=\"{}\", type=\"{}\"{}{}];\n",
            baseIndent_,
            indent_,
            nodeIdent(port),
            escape(!config_.rich ? label : wrapText(label, 7, 2)),
            nodeType,
            metadata,
            styling);
    };
    for (auto ref : graph->withPorts())
        emitPort(ref, "PORT");
    for (auto ref : graph->normPorts())
        emitPort(ref, "PORT");
    for (auto ref : graph->closureNodes())
        emitPort(ref, "CLOSURE_PORT");

    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const auto nodeRef = it.ref();
        const auto *node   = *it;
        if (!node || node->kind == camel::runtime::GCNodeKind::Port) {
            continue;
        }
        string label   = recordLabel(graph, nodeRef, *node);
        string tooltip = recordTooltip(graph, *node);
        string shape   = recordShape(*node);
        string style   = recordStyle(*node);
        string size    = recordSize(*node);
        string type    = recordType(*node);
        // Every real node carries its output value type. Export the compiler's type, including
        // static slots, rather than inferring it from the label or from the input types.
        const auto *outputType = nodeStaticSlotType(graph, *node);
        const string valueType =
            outputType ? camel::core::type::sourceTypeSpelling(outputType->toString()) : "";
        // The numeric permissions the program granted with @numeric(...) (design principles
        // 7.5): an attribute only on the nodes that have any, so that the dump of a program
        // without the annotation is unchanged.
        const uint8_t permissions = graph->nodePermissions(nodeRef);
        const string valueAttr =
            (valueType.empty() ? "" : std::format(", value_type=\"{}\"", escape(valueType))) +
            (permissions == 0 ? ""
                              : std::format(
                                    ", numeric=\"{}\"",
                                    camel::core::sem::permissionsText(permissions))) +
            // The attributes libraries attached to the node (language reference 7.6), likewise.
            (graph->nodeAttrs(nodeRef) == 0
                 ? ""
                 : std::format(
                       ", attrs=\"{}\"",
                       escape(camel::core::attr::text(graph->nodeAttrs(nodeRef)))));
        // Boundary roles: a role attribute; styled dumps also fill the node with its first role's
        // color and list every role in the tooltip.
        string roleAttr, roleFill, roleTooltip;
        if (config_.annotateRoles) {
            for (const auto &match :
                 camel::runtime::NodeRoleRegistry::instance().classify(*graph, nodeRef)) {
                roleAttr += (roleAttr.empty() ? "" : ";") + match.spec.name;
                roleTooltip += std::format("\\nrole {}: {}", match.spec.name, escape(match.detail));
                if (roleFill.empty()) {
                    roleFill = match.spec.color;
                }
                const bool seen =
                    std::any_of(rolesSeen_.begin(), rolesSeen_.end(), [&](const auto &r) {
                        return r.first == match.spec.name;
                    });
                if (!seen) {
                    rolesSeen_.push_back({match.spec.name, {match.spec.title, match.spec.color}});
                }
            }
        }
        if (!config_.rich) {
            res += std::format(
                "{}{}{} [label=\"{}\", type=\"{}\"{}{}];\n",
                baseIndent_,
                indent_,
                nodeIdent(node),
                escape(label),
                type,
                roleAttr.empty() ? "" : std::format(", role=\"{}\"", roleAttr),
                debugOriginAttr(graph, nodeRef) + valueAttr);
        } else {
            string tooltip = recordTooltip(graph, *node);
            res += std::format(
                "{}{}{} [label=\"{}\", type=\"{}\", shape={}, style={}{}{}, tooltip=\"{}\"];\n",
                baseIndent_,
                indent_,
                nodeIdent(node),
                escape(wrapText(label, 7, 2)),
                type,
                shape,
                roleFill.empty() ? style : std::format("\"filled,{}\"", style),
                (size.empty() ? "" : ", " + size) + valueAttr + debugOriginAttr(graph, nodeRef),
                roleFill.empty()
                    ? ""
                    : std::format(", fillcolor=\"{}\", role=\"{}\"", roleFill, roleAttr),
                std::format("{}\\n{}{}", escape(label), escape(tooltip), roleTooltip));
        }
    }

    for (size_t i = 0; i < graph->withPorts().size(); ++i) {
        const auto *port = lookupRecord(graph, graph->withPorts()[i]);
        ASSERT(port != nullptr, "Runtime with-port record is missing.");
        if (!config_.rich) {
            res += std::format(
                "{}{}{} -> {} [label=\"{}\", type=\"{}\"];\n",
                baseIndent_,
                indent_,
                funcId,
                nodeIdent(port),
                i,
                "WITH_PORT");
        } else {
            res += std::format(
                "{}{}{} -> {} [label=\"{}\", type=\"WITH_PORT\", style=dashed, "
                "arrowhead=empty];\n",
                baseIndent_,
                indent_,
                funcId,
                nodeIdent(port),
                i);
        }
    }
    for (size_t i = 0; i < graph->normPorts().size(); ++i) {
        const auto *port = lookupRecord(graph, graph->normPorts()[i]);
        ASSERT(port != nullptr, "Runtime norm-port record is missing.");
        if (!config_.rich) {
            res += std::format(
                "{}{}{} -> {} [label=\"{}\", type=\"{}\"];\n",
                baseIndent_,
                indent_,
                funcId,
                nodeIdent(port),
                i,
                "NORM_PORT");
        } else {
            res += std::format(
                "{}{}{} -> {} [label=\"{}\", type=\"NORM_PORT\", style=solid];\n",
                baseIndent_,
                indent_,
                funcId,
                nodeIdent(port),
                i);
        }
    }

    const auto *exitRecord = graph->exitNode();
    ASSERT(exitRecord != nullptr, "Runtime graph exit node is missing.");
    if (!config_.rich) {
        res += std::format(
            "{}{}{} -> {} [label=\"{}\", type=\"{}\"];\n",
            baseIndent_,
            indent_,
            nodeIdent(exitRecord),
            exitId,
            0,
            "EXIT");
    } else {
        res += std::format(
            "{}{}{} -> {} [label=\"{}\", type=\"EXIT\", style=solid];\n",
            baseIndent_,
            indent_,
            nodeIdent(exitRecord),
            exitId,
            0);
    }

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
            if (!config_.rich) {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"{}\"];\n",
                    baseIndent_,
                    indent_,
                    nodeIdent(input),
                    nodeIdent(node),
                    outIdx,
                    i,
                    "WITH");
            } else {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"WITH\", style=dashed];\n",
                    baseIndent_,
                    indent_,
                    nodeIdent(input),
                    nodeIdent(node),
                    outIdx,
                    i);
            }
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
            if (!config_.rich) {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"{}\"];\n",
                    baseIndent_,
                    indent_,
                    nodeIdent(input),
                    nodeIdent(node),
                    outIdx,
                    i,
                    "NORM");
            } else {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"NORM\"];\n",
                    baseIndent_,
                    indent_,
                    nodeIdent(input),
                    nodeIdent(node),
                    outIdx,
                    i);
            }
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
            if (!config_.rich) {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"{}\"];\n",
                    baseIndent_,
                    indent_,
                    nodeIdent(input),
                    nodeIdent(node),
                    outIdx,
                    i,
                    "CTRL");
            } else {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"CTRL\", style=dashed, "
                    "arrowhead=empty];\n",
                    baseIndent_,
                    indent_,
                    nodeIdent(input),
                    nodeIdent(node),
                    outIdx,
                    i);
            }
        }
    }

    if (depth_ == 0 && config_.annotateRoles && !rolesSeen_.empty()) {
        res += std::format("{}subgraph cluster_legend {{\n", indent_);
        res += std::format("{}{}label=\"roles\";\n", indent_, indent_);
        for (const auto &[name, spec] : rolesSeen_) {
            if (!config_.rich) {
                res += std::format(
                    "{}{}\"legend_{}\" [label=\"{}\", type=\"LEGEND\", role=\"{}\"];\n",
                    indent_,
                    indent_,
                    name,
                    escape(spec.first),
                    name);
            } else {
                res += std::format(
                    "{}{}\"legend_{}\" [label=\"{}\", type=\"LEGEND\", role=\"{}\", shape=box, "
                    "fixedsize=false, style=filled, fillcolor=\"{}\"];\n",
                    indent_,
                    indent_,
                    name,
                    escape(spec.first),
                    name,
                    spec.second);
            }
        }
        res += indent_ + "}\n";
    }
    res += baseIndent_ + "}\n";
    return res;
}

GraphVizDumpPass::GraphVizDumpPass(const context_ptr_t &context)
    : RuntimeGraphTranslatePass(context) {}

GraphVizDumpPass::GraphVizDumpPass(const context_ptr_t &context, GraphVizDumpConfig config)
    : RuntimeGraphTranslatePass(context), config_(std::move(config)) {}

std::string GraphVizDumpPass::dumpDot(camel::runtime::GCGraph *graph) { return dumpGraph(graph); }

std::string dumpCompactRuntimeGraph(camel::runtime::GCGraph *graph, const context_ptr_t &ctx) {
    if (!graph || !graph->hasNodePayload()) {
        return {};
    }
    GraphVizDumpConfig config;
    GraphVizDumpPass pass(ctx, std::move(config));
    return pass.dumpDot(graph);
}

camel::runtime::GCGraph *GraphVizDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    os << dumpGraph(graph);
    return nullptr;
}
