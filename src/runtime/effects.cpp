// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The effect summary of a graph (camel/runtime/effects.h).
 */

#include "camel/runtime/effects.h"

#include "camel/runtime/effect_liveness.h"

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
        const EffectSummary summary = ofNodes(graph, 0);
        active.erase(graph);
        done.emplace(graph, summary);
        return summary;
    }

    /// The nodes of `graph` that lie in `region` (0: all of them).
    EffectSummary ofNodes(const GCGraph *graph, uint32_t region) {
        const auto &table = camel::core::OperatorTable::instance();
        EffectSummary summary;
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            if (region != 0) {
                uint32_t at = graph->nodeRegion(ref);
                while (at != 0 && at != region && graph->region(at) != nullptr) {
                    at = graph->region(at)->parent;
                }
                if (at != region) {
                    continue;
                }
            }
            const GCNode *node = graph->node(ref);
            if (node == nullptr) {
                continue;
            }
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
                // The condition and the body are regions of this graph, so their effects are in
                // this graph's own summary; a loop may fault and may not terminate.
                summary.mayFault        = true;
                summary.mayNotTerminate = true;
                break;
            default:
                break;
            }
        }
        return summary;
    }
};

} // namespace

EffectSummary effectSummaryOf(const Program &program, const GCGraph *graph) {
    return EffectWalk{program}.of(graph);
}

EffectSummary
effectSummaryOfRegion(const Program &program, const GCGraph *graph, uint32_t region) {
    if (graph == nullptr || region == 0) {
        return {};
    }
    EffectWalk walk{program};
    walk.active.insert(graph);
    return walk.ofNodes(graph, region);
}

EffectStanding effectStandingOf(const EffectLivenessFacts &facts) {
    // 3.1's two: a value of it is read, or it is a statement of a `sync` block. A read written
    // after it takes what it wrote only where the writer table says so.
    // `isWriterWithLaterRead` is **not** read here: no caller fills it (see the field's comment), so
    // it is false everywhere, and a credential that can never be true does not belong in the rule -- it
    // only makes a reader believe a third credential exists. 3.1's third credential is not implemented
    // in this layer today; when the writer table's answer is measured, it comes back here with a
    // caller.
    if (facts.hasDataUser || facts.isSyncStatement) {
        return EffectStanding::Alive;
    }
    // A call through a function value is not a third answer: the writer table records it as the
    // variable's write, the call is that variable's new version, and the read written after it
    // takes a data edge from the call (Builder::valueVersionOf). A write nothing here knows about
    // is left to the sync blocks and the resource order (docs/design-principles.cn.md 3.3); it is
    // not a credential of this layer, which is why it does not make a node alive.
    return EffectStanding::HasNoCredential;
}

bool keepsEffectAlive(EffectStanding standing) {
    return standing != EffectStanding::HasNoCredential;
}

} // namespace camel::runtime
