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
 * Author: Camel Contributors
 * Created: Sep. 30, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * std::stats (see stats.h).
 */

#include "stats.h"

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
        "\"indirect_calls\":{},\"closures\":{},\"branches\":{},\"loops\":{},\"bytes\":{}}}",
        graphs,
        nodes,
        edges,
        opers,
        directCalls,
        indirectCalls,
        closures,
        branches,
        loops,
        bytes);
}

GraphStats collectGraphStats(const camel::core::context::context_ptr_t &context, GCGraph *root) {
    const camel::runtime::Program *program =
        root && context ? context->programOfRuntimeGraph(root) : nullptr;
    return program ? collectGraphStats(*program, root) : GraphStats{};
}

GraphStats collectGraphStats(const camel::runtime::Program &program, GCGraph *root) {
    GraphStats stats;
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
