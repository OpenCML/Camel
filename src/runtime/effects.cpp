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
 * Created: Oct. 15, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The effect summary of a graph (camel/runtime/effects.h).
 */

#include "camel/runtime/effects.h"

#include "camel/core/operator_table.h"

#include <unordered_map>
#include <unordered_set>

namespace camel::runtime {

namespace {

struct EffectWalk {
    const Program &program;
    std::unordered_map<const GCGraph *, EffectSummary> done;
    std::unordered_set<const GCGraph *> active;

    EffectSummary of(const GCGraph *graph) {
        if (graph == nullptr) {
            return {};
        }
        if (auto it = done.find(graph); it != done.end()) {
            return it->second;
        }
        if (!active.insert(graph).second) {
            // A graph that reaches itself: a recursion, which may not end. What it does
            // besides is what the walk that is under way collects.
            EffectSummary recursion;
            recursion.mayNotTerminate = true;
            return recursion;
        }
        const auto &table = camel::core::OperatorTable::instance();
        EffectSummary summary;
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            switch (node->kind) {
            case GCNodeKind::Oper: {
                const auto oper   = graph->nodeBodyAs<GCOperBody>(ref)->oper;
                const auto traits = table.find(oper);
                summary.external |= table.hasExternalEffect(oper);
                // What an operator writes is its resource declaration's (design principles 3.3).
                summary.writesVariables |= table.writesProgramState(oper);
                summary.mayFault |= !traits || traits->mayFault;
                if (traits && traits->callsGraphs) {
                    // It runs the function values it receives: not known here.
                    summary.external        = true;
                    summary.mayNotTerminate = true;
                }
                break;
            }
            case GCNodeKind::Call:
                summary.external        = true;
                summary.mayFault        = true;
                summary.mayNotTerminate = true;
                break;
            case GCNodeKind::Func:
                summary |= of(program.calleeOf(*graph, ref));
                break;
            case GCNodeKind::Loop:
                summary |= of(program.loopCondOf(*graph, ref));
                summary |= of(program.loopBodyOf(*graph, ref));
                summary.mayFault        = true;
                summary.mayNotTerminate = true;
                break;
            default:
                break;
            }
        }
        active.erase(graph);
        done.emplace(graph, summary);
        return summary;
    }
};

} // namespace

EffectSummary effectSummaryOf(const Program &program, const GCGraph *graph) {
    return EffectWalk{program}.of(graph);
}

} // namespace camel::runtime
