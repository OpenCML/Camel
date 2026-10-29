// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "graphviz.h"
#include "camel/core/node_attrs.h"
#include "camel/core/tags.h"
#include "camel/core/operator_semantics.h"
#include "camel/passes/trans/gir_metadata.h"

#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/source/ids.h"
#include "camel/core/source/manager.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/node_roles.h"
#include "camel/runtime/reachable.h"
#include "camel/runtime/writings.h"
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

    // The classes of a graph whose values have several writings: every class is drawn as a
    // dashed cluster of its writings, the writing extraction would take is marked, and an input
    // edge of a writing leaves the class of the value it reads. A graph with one writing per
    // value (the executable state) has none of this and is dumped exactly as before.
    const auto graphClasses = [&]() {
        const auto model = camel::runtime::findCostModel(camel::runtime::kWritingsCostModel);
        if (!model || graph->executable()) {
            return camel::runtime::GraphWritings{};
        }
        return camel::runtime::graphWritings(*program_, graph, *model, context_.get());
    }();
    // The class a node is a writing of, or nullptr when its value has one writing only.
    const auto classOf = [&](camel::runtime::gc_node_ref_t node) -> const camel::runtime::GraphWritings::Value * {
        const auto at = graphClasses.valueOf.find(node);
        return at == graphClasses.valueOf.end() ? nullptr : &graphClasses.values.at(at->second);
    };
    // The name of the class of a class: `c<smallest writing>`, and the DOT name of its class node.
    const auto classLabel = [](camel::runtime::gc_node_ref_t root) {
        return std::format("c{}", root);
    };
    const auto classIdent = [&](camel::runtime::gc_node_ref_t root) {
        const camel::runtime::GCNode *record = graph->node(root);
        return record != nullptr ? pointerToIdent(record, "C") : std::format("C{}", root);
    };
    /// The node an input edge leaves: the class node of the value when the value has several
    /// writings (the input end of an edge names a value, not one of its writings), the writing
    /// itself otherwise. A class node stands for one value and has one output, so the source-side
    /// index of such an edge is 0 (`readsAClass` tells the caller, which then need not look for
    /// the consumer in the writing's outputs).
    const auto edgeSource = [&](camel::runtime::gc_node_ref_t input) -> std::string {
        if (const auto *value = classOf(input); value != nullptr) {
            return classIdent(value->root);
        }
        const auto *record = lookupRecord(graph, input);
        ASSERT(record != nullptr, "Runtime input record is missing.");
        return nodeIdent(record);
    };
    const auto readsAClass = [&](camel::runtime::gc_node_ref_t input) {
        return classOf(input) != nullptr;
    };

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
    // The facts of the body region (0): the tags written on the function. A graph whose body
    // carries none prints no line, so existing output is unchanged.
    if (graph->regionCount() > 0 && graph->region(0)->attrs != camel::core::attr::kNoAttrs) {
        res += std::format(
            "{}{}body_attrs=\"{}\";\n",
            baseIndent_,
            indent_,
            escape(camel::core::attr::text(graph->region(0)->attrs)));
    }
    // The compact view prints the fact of each tagged region as a line of its own. A cluster is
    // a layout (`std::gir_regions` draws it); the fact is graph content and must be visible in
    // the view used for review and goldens too. A region without a fact
    // prints nothing, so existing output is unchanged.
    if (!config_.regions) {
        for (uint32_t region = 1; region < graph->regionCount(); ++region) {
            const auto &record = *graph->region(region);
            if (record.attrs == camel::core::attr::kNoAttrs) {
                continue;
            }
            const char *kind = "region";
            switch (record.kind) {
            case camel::runtime::GCRegionKind::Arm:
                kind = "arm";
                break;
            case camel::runtime::GCRegionKind::LoopCond:
                kind = "loop_cond";
                break;
            case camel::runtime::GCRegionKind::LoopBody:
                kind = "loop_body";
                break;
            case camel::runtime::GCRegionKind::Inline:
                kind = "inline";
                break;
            case camel::runtime::GCRegionKind::Block:
                kind = "block";
                break;
            default:
                break;
            }
            res += std::format(
                "{}{}region #{} {} parent={} attrs=\"{}\";\n",
                baseIndent_,
                indent_,
                region,
                kind,
                record.parent,
                escape(camel::core::attr::text(record.attrs)));
        }
    }

    // Which cost model marks the writing extraction would take: the model the pipeline itself
    // extracts by. A graph with one writing per value prints nothing here.
    if (!graphClasses.empty()) {
        res += std::format(
            "{}{}writings_cost=\"{}\";\n",
            baseIndent_,
            indent_,
            camel::runtime::kWritingsCostModel);
    }

    program_->traceGraphs(*graph, [&](camel::runtime::GCGraph *dep) {
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

    /// One node of the graph: its line, and the cluster of the region it is in. A node whose value
    /// has several writings carries the class it is a writing of, and the writing extraction would
    /// take says so.
    const auto emitNode = [&](camel::runtime::gc_node_ref_t nodeRef,
                              const camel::runtime::GCNode *node) {
        string label   = recordLabel(*program_, graph, nodeRef, *node);
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
        // The numeric permissions the program granted with permission tags (design principles
        // 7.2) and the attributes libraries attached to the node (language reference 7.6), read
        // through the one query for the facts of a node (camel/core/tags.h): an attribute only
        // on the nodes that have any, so that the dump of a program without tags is unchanged.
        const camel::core::tag::Facts facts = camel::core::tag::factsOf(*graph, nodeRef);
        const camel::core::sem::PermissionMask permissions = facts.permissions;
        const string valueAttr =
            (valueType.empty() ? "" : std::format(", value_type=\"{}\"", escape(valueType))) +
            (permissions == 0 ? ""
                              : std::format(
                                    ", numeric=\"{}\"",
                                    camel::core::sem::permissionsText(permissions))) +
            (facts.tags == 0
                 ? ""
                 : std::format(
                       ", attrs=\"{}\"", escape(camel::core::attr::text(facts.tags))));
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
        const size_t lineStart = res.size();
        // The class of the value this node is a writing of, when the value has several: the
        // writing names its class, and the one extraction would take is marked (a heavier border
        // in the styled dump)..
        string classAttr;
        if (const auto *value = classOf(nodeRef); value != nullptr) {
            classAttr = std::format(", class=\"{}\"", classLabel(value->root));
            if (value->chosen == nodeRef) {
                classAttr += config_.rich ? ", chosen=\"true\", penwidth=2" : ", chosen=\"true\"";
            }
        }
        if (!config_.rich) {
            res += std::format(
                "{}{}{} [label=\"{}\", type=\"{}\"{}{}{}];\n",
                baseIndent_,
                indent_,
                nodeIdent(node),
                escape(label),
                type,
                roleAttr.empty() ? "" : std::format(", role=\"{}\"", roleAttr),
                debugOriginAttr(graph, nodeRef) + valueAttr,
                classAttr);
        } else {
            string tooltip = recordTooltip(graph, *node);
            res += std::format(
                "{}{}{} [label=\"{}\", type=\"{}\", shape={}, style={}{}{}, tooltip=\"{}\"{}];\n",
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
                std::format("{}\\n{}{}", escape(label), escape(tooltip), roleTooltip),
                classAttr);
        }
        // A node in a region stands in the cluster of the region, inside those of the regions
        // it is in; dot merges the clusters of one name.
        if (config_.regions && graph->regionCount() > 1 && graph->nodeRegion(nodeRef) != 0) {
            std::vector<uint32_t> chain;
            for (uint32_t r = graph->nodeRegion(nodeRef); r != 0; r = graph->region(r)->parent) {
                chain.push_back(r);
            }
            string line = res.substr(lineStart);
            res.resize(lineStart);
            for (auto at = chain.rbegin(); at != chain.rend(); ++at) {
                const camel::runtime::GCRegion &region = *graph->region(*at);
                string title                           = "region";
                switch (region.kind) {
                case camel::runtime::GCRegionKind::Arm:
                    title = std::format("arm {}", region.index);
                    break;
                case camel::runtime::GCRegionKind::LoopCond:
                    title = "loop condition";
                    break;
                case camel::runtime::GCRegionKind::LoopBody:
                    title = "loop body";
                    break;
                case camel::runtime::GCRegionKind::Inline: {
                    const auto *source = program_->graph(region.inlineOf);
                    title              = "inlined " + (source ? source->name() : string("?"));
                    break;
                }
                case camel::runtime::GCRegionKind::Block:
                    title = "block";
                    break;
                default:
                    break;
                }
                res += std::format(
                    "{}{}subgraph cluster_{}_r{} {{ label=\"{}\";\n",
                    baseIndent_,
                    indent_,
                    funcId,
                    *at,
                    escape(title));
                if (region.attrs != camel::core::attr::kNoAttrs) {
                    res += std::format(
                        "{}{}region_attrs=\"{}\";\n",
                        baseIndent_,
                        indent_,
                        escape(camel::core::attr::text(region.attrs)));
                }
            }
            res += line;
            for (size_t i = 0; i < chain.size(); ++i) {
                res += baseIndent_ + indent_ + "}\n";
            }
        }
    };

    // The values with several writings, each as a dashed cluster: the class node stands for the
    // value, every writing of it is a node inside, and the writing extraction would take is
    // marked. A graph in the executable state has no class here and its dump is unchanged.
    for (const auto &value : graphClasses.values) {
        const string ident   = classIdent(value.root);
        const string label   = classLabel(value.root);
        res += std::format("{}{}subgraph cluster_class_{} {{\n", baseIndent_, indent_, ident);
        res += std::format("{}{}label=\"class {}\";\n", baseIndent_, indent_, label);
        res += std::format("{}{}style=dashed;\n", baseIndent_, indent_);
        if (!config_.rich) {
            res += std::format(
                "{}{}{} [label=\"class {}\", type=\"CLASS\"];\n",
                baseIndent_,
                indent_,
                ident,
                label);
        } else {
            res += std::format(
                "{}{}{} [label=\"class {}\", type=\"CLASS\", shape=box, style=dashed, "
                "fixedsize=false];\n",
                baseIndent_,
                indent_,
                ident,
                label);
        }
        pushIndent();
        for (camel::runtime::gc_node_ref_t member : value.writings) {
            if (const camel::runtime::GCNode *record = graph->node(member); record != nullptr) {
                emitNode(member, record);
            }
        }
        popIndent();
        res += baseIndent_ + indent_ + "}\n";
    }

    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const auto nodeRef = it.ref();
        const auto *node   = *it;
        if (!node || node->kind == camel::runtime::GCNodeKind::Port) {
            continue;
        }
        if (classOf(nodeRef) != nullptr) {
            continue; // written in the cluster of its class, above
        }
        emitNode(nodeRef, node);
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
    // The graph's result is a value: the edge leaves its class when it has several writings.
    const std::string exitSource = edgeSource(graph->nodeRef(exitRecord));
    if (!config_.rich) {
        res += std::format(
            "{}{}{} -> {} [label=\"{}\", type=\"{}\"];\n",
            baseIndent_,
            indent_,
            exitSource,
            exitId,
            0,
            "EXIT");
    } else {
        res += std::format(
            "{}{}{} -> {} [label=\"{}\", type=\"EXIT\", style=solid];\n",
            baseIndent_,
            indent_,
            exitSource,
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
            // The input end names a value: a value with several writings is read through its
            // class node, which has one output, so no output slot has to be found.
            if (!readsAClass(inputIndex)) {
                for (auto outIndex : graph->withOutputsOf(inputIndex)) {
                    if (outIndex == nodeIndex) {
                        break;
                    }
                    outIdx++;
                }
            }
            if (!config_.rich) {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"{}\"];\n",
                    baseIndent_,
                    indent_,
                    edgeSource(inputIndex),
                    nodeIdent(node),
                    outIdx,
                    i,
                    "WITH");
            } else {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"WITH\", style=dashed];\n",
                    baseIndent_,
                    indent_,
                    edgeSource(inputIndex),
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
            if (!readsAClass(inputIndex)) {
                for (auto outIndex : graph->normOutputsOf(inputIndex)) {
                    if (outIndex == nodeIndex) {
                        break;
                    }
                    outIdx++;
                }
            }
            if (!config_.rich) {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"{}\"];\n",
                    baseIndent_,
                    indent_,
                    edgeSource(inputIndex),
                    nodeIdent(node),
                    outIdx,
                    i,
                    "NORM");
            } else {
                res += std::format(
                    "{}{}{} -> {} [label=\"{}|{}\", type=\"NORM\"];\n",
                    baseIndent_,
                    indent_,
                    edgeSource(inputIndex),
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

        // An order edge and a sync edge are not ports, so they are drawn from the graph's own
        // arrays rather than from an input run (docs/design/effects/edges.cn.md 1.5). They are
        // drawn apart from the port edges and from each other: an order edge only orders (dotted,
        // no arrowhead filled in), a sync edge orders *and* is used (solid, diamond arrowhead).
        for (const camel::runtime::GCGraphOrderEdge &edge : graph->orderPredsOf(nodeIndex)) {
            if (!graph->containsNodeRef(edge.pred)) {
                continue;
            }
            if (!config_.rich) {
                res += std::format(
                    "{}{}{} -> {} [label=\"ORDER\", type=\"ORDER\"];\n",
                    baseIndent_,
                    indent_,
                    edgeSource(edge.pred),
                    nodeIdent(node));
            } else {
                res += std::format(
                    "{}{}{} -> {} [label=\"ORDER\", type=\"ORDER\", style=dotted];\n",
                    baseIndent_,
                    indent_,
                    edgeSource(edge.pred),
                    nodeIdent(node));
            }
        }
        for (const camel::runtime::GCGraphSyncEdge &edge : graph->syncPredsOf(nodeIndex)) {
            if (!graph->containsNodeRef(edge.pred)) {
                continue;
            }
            if (!config_.rich) {
                res += std::format(
                    "{}{}{} -> {} [label=\"SYNC\", type=\"SYNC\"];\n",
                    baseIndent_,
                    indent_,
                    edgeSource(edge.pred),
                    nodeIdent(node));
            } else {
                res += std::format(
                    "{}{}{} -> {} [label=\"SYNC\", type=\"SYNC\", style=solid, "
                    "arrowhead=diamond];\n",
                    baseIndent_,
                    indent_,
                    edgeSource(edge.pred),
                    nodeIdent(node));
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

std::string GraphVizDumpPass::dumpDot(camel::runtime::GCGraph *graph) {
    program_ = context_->programOfRuntimeGraph(graph);
    ASSERT(program_ != nullptr, "A graph to dump belongs to no program of the context.");
    return dumpGraph(graph);
}

std::string dumpCompactRuntimeGraph(camel::runtime::GCGraph *graph, const context_ptr_t &ctx) {
    if (!graph || !graph->hasNodePayload()) {
        return {};
    }
    GraphVizDumpConfig config;
    GraphVizDumpPass pass(ctx, std::move(config));
    return pass.dumpDot(graph);
}

const camel::runtime::Program *
GraphVizDumpPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    program_                       = program;
    os << dumpGraph(graph);
    return nullptr;
}

