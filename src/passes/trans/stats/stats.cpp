// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::stats (see stats.h).
 */

#include "stats.h"

#include "camel/runtime/cycle.h"
#include "camel/runtime/reachable.h"

#include <format>
#include <ostream>

using camel::runtime::GCFillBody;
using camel::runtime::GCFillKind;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;

std::string GraphStats::toJson() const {
    return std::format(
        "{{\"graphs\":{},\"nodes\":{},\"edges\":{},\"opers\":{},\"direct_calls\":{},"
        "\"indirect_calls\":{},\"closures\":{},\"branches\":{},\"loops\":{},\"bytes\":{},"
        "\"specializations\":{},\"cycle_dropped_sessions\":{}}}",
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
        cycleDroppedSessions);
}

GraphStats collectGraphStats(const camel::core::context::context_ptr_t &context, GCGraph *root) {
    const camel::runtime::Program *program =
        root && context ? context->programOfRuntimeGraph(root) : nullptr;
    return program ? collectGraphStats(*program, root) : GraphStats{};
}

GraphStats collectGraphStats(const camel::runtime::Program &program, GCGraph *root) {
    GraphStats stats;
    stats.specializations = program.specializations().size();
    stats.cycleDroppedSessions = camel::runtime::cycleDroppedSessions();
    if (!root) {
        return stats;
    }
    for (GCGraph *graph : camel::runtime::collectReachableGraphs(program, root)) {
        ++stats.graphs;
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
