// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The effect summary of a graph (camel/runtime/effects.h).
 */

#include "camel/runtime/effects.h"

#include "camel/runtime/effect_liveness.h"

#include "camel/core/operator_table.h"

#include <algorithm>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace camel::runtime {

namespace {

struct EffectWalk {
    // program-holder-exempt: borrow-or-foreign-type -- a walk over the program of the call that
    // made it; effects() holds it for the walk.
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
    if (facts.hasDataUser || facts.isSyncStatement || facts.isWriterWithLaterRead) {
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

namespace {

/// The places a node writes into, as its declaration names them: every ordinary input
/// `OperatorTable::writesOperand` says it writes. A node that creates a cell of its own writes into
/// no operand -- the cell is the node's own value, which `deadWrite` asks about on its own.
std::vector<gc_node_ref_t> writtenPlacesOf(const GCGraph &graph, gc_node_ref_t ref) {
    std::vector<gc_node_ref_t> places;
    const auto *body = graph.nodeBodyAs<GCOperBody>(ref);
    if (body == nullptr) {
        return places;
    }
    const auto &table = camel::core::OperatorTable::instance();
    const auto inputs = graph.normInputsOf(ref);
    for (size_t index = 0; index < inputs.size(); ++index) {
        if (table.writesOperand(body->oper, false, index)) {
            places.push_back(inputs[index]);
        }
    }
    return places;
}

} // namespace

bool valueIsRooted(const GCGraph &graph, gc_node_ref_t ref) {
    if (ref == kInvalidNodeRef || !graph.containsNodeRef(ref)) {
        return false;
    }
    const GCNode *node = graph.node(ref);
    if (node == nullptr || node->kind == GCNodeKind::Dead) {
        return false;
    }
    if (ref == graph.entryNodeRef() || ref == graph.exitNodeRef() ||
        ref == graph.outputNodeRef() || ref == graph.returnNodeRef()) {
        return true;
    }
    const auto names = [ref](std::span<const gc_node_ref_t> values) {
        return std::find(values.begin(), values.end(), ref) != values.end();
    };
    if (names(graph.normPorts()) || names(graph.withPorts()) || names(graph.closureNodes())) {
        return true;
    }
    for (uint32_t index = 0; index < graph.regionCount(); ++index) {
        const GCRegion *region = graph.region(index);
        if (region != nullptr && region->result == ref) {
            return true;
        }
    }
    return false;
}

bool valueIsRead(const GCGraph &graph, gc_node_ref_t ref) {
    return !graph.normUsersOf(ref).empty() || !graph.withUsersOf(ref).empty();
}

bool writeIsRooted(const GCGraph &graph, gc_node_ref_t ref) {
    // The two things a root can hold: the value the write leaves, and the place it writes into. The
    // place is what a capture port or a `var` parameter is -- the graph names it, so the program
    // keeps it whatever a rewrite does with the write itself.
    if (valueIsRooted(graph, ref)) {
        return true;
    }
    for (gc_node_ref_t place : writtenPlacesOf(graph, ref)) {
        if (valueIsRooted(graph, place)) {
            return true;
        }
    }
    return false;
}

bool deadWrite(const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    if (node == nullptr || node->kind != GCNodeKind::Oper) {
        return false;
    }
    return !writeIsRooted(graph, ref) && !valueIsRead(graph, ref);
}

} // namespace camel::runtime
