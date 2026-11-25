// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::stats (see stats.h).
 */

#include "stats.h"

#include "camel/execute/pass/registration.h"
#include "camel/runtime/cycle.h"
#include "camel/runtime/reachable.h"

#include <format>
#include <ostream>

using camel::runtime::GCFillBody;
using camel::runtime::GCFillKind;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;

namespace {

/// A JSON string literal for `text`. Graph names are source identifiers, so this normally writes
/// them unchanged; the reader of the line is a parser, and a name that held a quote or a backslash
/// would end the object early if it were written as it is.
std::string jsonString(const std::string &text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out +=
                    std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
            } else {
                out += c;
            }
            break;
        }
    }
    out += '"';
    return out;
}

} // namespace

std::string GraphStats::toJson() const {
    std::string names;
    std::string macro;
    for (const GraphEntry &entry : graphEntries) {
        if (!names.empty()) {
            names += ',';
            macro += ',';
        }
        names += jsonString(entry.name);
        macro += entry.macro ? "true" : "false";
    }
    return std::format(
        "{{\"graphs\":{},\"nodes\":{},\"edges\":{},\"opers\":{},\"direct_calls\":{},"
        "\"indirect_calls\":{},\"closures\":{},\"branches\":{},\"loops\":{},\"bytes\":{},"
        "\"specializations\":{},\"cycle_dropped_sessions\":{},\"graph_names\":[{}],"
        "\"macro\":[{}]}}",
        graphs,
        nodes,
        edges,
        opers,
        directCalls,
        indirectCalls,
        closures,
        branches,
        loops,
        bytes,
        specializations,
        cycleDroppedSessions,
        names,
        macro);
}

GraphStats collectGraphStats(const camel::core::context::context_ptr_t &context, GCGraph *root) {
    const camel::runtime::Program *program =
        root && context ? context->programOfRuntimeGraph(*root) : nullptr;
    return program ? collectGraphStats(*program, root) : GraphStats{};
}

GraphStats collectGraphStats(const camel::runtime::Program &program, GCGraph *root) {
    GraphStats stats;
    stats.specializations      = program.specializations().size();
    stats.cycleDroppedSessions = camel::runtime::cycleDroppedSessions();
    if (!root) {
        return stats;
    }
    for (auto *graph : camel::runtime::collectReachableGraphs(program, root)) {
        ++stats.graphs;
        // The graph the walk already holds, and its own derived answer: `isMacro` is a fact of the
        // GCGraph (camel/runtime/graph.h#GCGraph::isMacro), not of a view or of a flag kept beside
        // it, so reading it here adds no traversal and no second derivation.
        stats.graphEntries.push_back({graph->name(), graph->isMacro()});
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto ref   = it.ref();
            const auto *node = *it;
            ++stats.nodes;
            const size_t inputs = graph->normInputsOf(ref).size() +
                                  graph->withInputsOf(ref).size() + graph->ctrlInputsOf(ref).size();
            stats.edges += inputs;
            stats.bytes += sizeof(camel::runtime::GCNode) +
                           inputs * sizeof(camel::runtime::gc_node_ref_t) +
                           graph->nodePayloadBytes(ref).size();
            switch (node->kind) {
            case GCNodeKind::Oper:
                ++stats.opers;
                break;
            case GCNodeKind::Func:
                ++stats.directCalls;
                break;
            case GCNodeKind::Call:
                ++stats.indirectCalls;
                break;
            case GCNodeKind::Brch:
                ++stats.branches;
                break;
            case GCNodeKind::Loop:
                ++stats.loops;
                break;
            case GCNodeKind::Fill:
                if (graph->nodeBodyAs<GCFillBody>(ref)->fillKind == GCFillKind::FunctionClosure) {
                    ++stats.closures;
                }
                break;
            default:
                break;
            }
        }
    }
    return stats;
}

const camel::runtime::Program *
GraphStatsPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    os << "GRAPH_STATS " << collectGraphStats(*program, program->root()).toJson() << std::endl;
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// `std::stats`: the name and the parameters are the declaration in stdlib/std.cml; this is the
/// kernel and nothing else (design principles 9.3, camel/execute/pass/registration.h).
const PassKernel statsKernel("std::stats", makePass<GraphStatsPass>());

} // namespace
