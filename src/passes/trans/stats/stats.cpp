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
 * Updated: Sep. 30, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * std::stats (see stats.h).
 */

#include "stats.h"

#include "camel/runtime/draft_session.h"

#include <format>
#include <ostream>

using camel::runtime::GCFillBody;
using camel::runtime::GCFillKind;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;

std::string GraphStats::toJson() const {
    return std::format(
        "{{\"graphs\":{},\"nodes\":{},\"edges\":{},\"opers\":{},\"direct_calls\":{},"
        "\"indirect_calls\":{},\"closures\":{},\"branches\":{}}}",
        graphs,
        nodes,
        edges,
        opers,
        directCalls,
        indirectCalls,
        closures,
        branches);
}

GraphStats collectGraphStats(const camel::core::context::context_ptr_t &context, GCGraph *root) {
    GraphStats stats;
    if (!root) {
        return stats;
    }
    camel::runtime::RuntimeGraphDraftSession session(context, root);
    for (GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        ++stats.graphs;
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto ref = it.ref();
            const auto *node = *it;
            ++stats.nodes;
            stats.edges += graph->normInputsOf(ref).size() + graph->withInputsOf(ref).size() +
                           graph->ctrlInputsOf(ref).size();
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

GCGraph *GraphStatsPass::apply(GCGraph *graph, std::ostream &os) {
    os << "GRAPH_STATS " << collectGraphStats(context_, graph).toJson() << std::endl;
    return graph;
}
