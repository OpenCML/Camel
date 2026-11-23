// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The effect summary of a graph (camel/runtime/effects.h): the value, the operator clause, and the
 * one computation of it that the fact layer registers (`effect_walk.h`).
 */

#include "camel/runtime/effects.h"

#include "effect_walk.h"

#include "camel/runtime/effect_liveness.h"

#include "camel/core/operator_table.h"
#include "camel/core/resource_semantics.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/resources.h"

#include <algorithm>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace camel::runtime {

namespace {

/// What a higher-order operator's function operand amounts to, when its declaration takes the
/// accesses from there (design principles 3.3): the clause of the function value
/// that arrives in `with` operand `functionOperand` -- the rule a call through a value follows. The
/// conservative answer is the default, and it is what an absent or unreadable value gets: the walk
/// must never report "touches nothing" because it could not read something.
struct OperandClause {
    bool external        = true;
    bool writesVariables = true;
};

/// What a clause amounts to in the two answers this summary can speak: whether it reaches outside
/// the program, and whether a parameter it writes is program state. `pure` is a clause -- the one
/// that names nothing -- so it answers "no" to both; an absent clause (`Generic`, or a value the
/// walk cannot read) answers "yes" to both, because a clause nobody wrote is not an empty one.
OperandClause clauseReach(const std::optional<core::sem::ResourceSignature> &clause) {
    if (!clause.has_value()) {
        return OperandClause{};
    }
    OperandClause out;
    out.external        = clause->external;
    out.writesVariables = false;
    for (const auto &use : clause->params) {
        const bool writes = use.mode == core::sem::AccessMode::Write ||
                            use.mode == core::sem::AccessMode::ReadModifyWrite;
        if (writes && (use.cls == core::sem::ResourceClass::Variable ||
                       use.cls == core::sem::ResourceClass::Object)) {
            out.writesVariables = true;
        }
    }
    return out;
}

/// The half of its answer the operator states itself: an `externalEffect`, and an access whose
/// resource is outside the program (`sem::declaresOutside`). What it leaves out is the "may reach
/// outside" an operator answers when its accesses are a function operand's (`OperatorSemantics::
/// external`, `Record::external`): that half is the value's that arrives in the operand, and it is
/// read at the node. A node's effect is the union of the two halves (design principles 3.3), which
/// is what `effectSummaryOfOperator` below states.
///
/// The access list is asked where the operator states one; an operator that states only a word
/// (`OperatorTable::set`, which is how most of the standard library says `pure`) is read through
/// the traits that word produced, and an operator nobody declared reaches outside (design
/// principles 3.3). Only an itemized list can say "the accesses are my operand's", so the two
/// readings agree wherever they overlap.
bool declaresExternalAccess(
    const std::optional<camel::core::OperatorTraits> &traits,
    const core::sem::OperatorSemantics *facts) {
    if (facts == nullptr || !facts->resources) {
        return !traits || (!traits->pure && !traits->stateOnly);
    }
    return core::sem::declaresOutside(*facts->resources);
}

OperandClause functionOperandClause(const GCGraph &graph, gc_node_ref_t ref) {
    const auto &table       = camel::core::OperatorTable::instance();
    const core::OperId oper = graph.nodeBodyAs<GCOperBody>(ref)->oper;
    const auto *facts       = table.semantics(oper);
    if (facts == nullptr || !facts->resources || !facts->resources->functionOperand) {
        return OperandClause{};
    }
    // The operand lookup, the gates and the base are the runtime footprint's own reading
    // (`clauseOfOperand`), so this summary and the check it feeds cannot disagree.
    return clauseReach(clauseOfOperand(graph, ref, *facts->resources->functionOperand).clause);
}

/// The same reading for a call through a function value: the callee is `with` input 0, and its
/// clause is what the call reaches.
OperandClause callClause(const GCGraph &graph, gc_node_ref_t ref) {
    return clauseReach(clauseOfCall(graph, ref).clause);
}

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
                // The one reading of an operator's effect, at the node it runs at
                // (`effectSummaryOfOperator`, which `core.operatorEffects` and a per-node reader
                // ask as well), so this summary and those readers cannot disagree.
                summary |=
                    effectSummaryOfOperator(graph->nodeBodyAs<GCOperBody>(ref)->oper, graph, ref);
                break;
            }
            case GCNodeKind::Call:
                // A call through a function value: its reach is the value's clause, read the same
                // way `Resolver::call` reads it for the footprint (design principles 3.3). `pure`
                // is the clause that names nothing, so a call through a pure value is not an
                // external effect. A reader that answers from no type at all calls every call
                // "external", `pure` included, which is what makes a pure call look like a write
                // nobody can name.
                summary.external |= callClause(*graph, ref).external;
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

namespace effectwalk {

EffectSummary ofGraph(const Program &program, const GCGraph *graph) {
    return EffectWalk{program}.of(graph);
}

EffectSummary ofRegion(const Program &program, const GCGraph *graph, uint32_t region) {
    if (graph == nullptr || region == 0) {
        return {};
    }
    EffectWalk walk{program};
    walk.active.insert(graph);
    return walk.ofNodes(graph, region);
}

} // namespace effectwalk

EffectSummary
effectSummaryOfOperator(camel::core::OperId oper, const GCGraph *graph, gc_node_ref_t ref) {
    const auto &table = camel::core::OperatorTable::instance();
    const std::optional<camel::core::OperatorTraits> traits = table.find(oper);
    EffectSummary summary;
    summary.mayFault |= !traits || traits->mayFault;
    // The half the declaration states: fixed, whatever the operands are (design principles 3.3).
    summary.external |= declaresExternalAccess(traits, table.semantics(oper));
    // What an operator writes is its resource declaration's (design principles 3.3).
    summary.writesVariables |= table.writesProgramState(oper);
    if (traits && traits->callsGraphs) {
        // It runs a function the program hands it, so the node's effect is also what that
        // function does: the clause of the function value that arrives in the operand the
        // declaration names (design principles 3.3). The two halves are united and neither one
        // replaces the other -- an operator declared to reach outside is external even when the
        // value it is handed is pure.
        summary.mayNotTerminate = true;
        if (graph == nullptr) {
            // No node to read the value off: the conservative answer for the half an operand
            // carries, because a clause the walk cannot read is not an empty one.
            summary.external = true;
        } else {
            const OperandClause clause = functionOperandClause(*graph, ref);
            summary.external |= clause.external;
            summary.writesVariables |= clause.writesVariables;
        }
    }
    return summary;
}

EffectSummary declaredEffectsOfOperator(camel::core::OperId oper) {
    // A reader with no node to read a function operand's clause off (a draft under rewrite).
    return effectSummaryOfOperator(oper, nullptr, 0);
}

namespace effectwalk {

EffectSummary ofNode(const Program &program, const GCGraph *graph, gc_node_ref_t ref) {
    EffectSummary summary;
    if (graph == nullptr || graph->node(ref) == nullptr) {
        return summary;
    }
    switch (graph->node(ref)->kind) {
    case GCNodeKind::Oper:
        // The one reading of an operator's effect, at this node (`effectSummaryOfOperator`).
        summary |= effectSummaryOfOperator(graph->nodeBodyAs<GCOperBody>(ref)->oper, graph, ref);
        break;
    case GCNodeKind::Call:
        // A call through a function value: its reach is the value's clause (design principles 3.3),
        // and nothing says what it runs may end or cannot fault.
        summary.external |= callClause(*graph, ref).external;
        summary.mayFault        = true;
        summary.mayNotTerminate = true;
        break;
    case GCNodeKind::Func:
        return ofGraph(program, program.calleeOf(*graph, ref));
    case GCNodeKind::Loop:
        summary.mayFault        = true;
        summary.mayNotTerminate = true;
        break;
    default:
        // A value, a port, a gate, a branch: it computes (or hands on) a value, and what runs
        // inside a branch is a region of this graph rather than this node's own running.
        break;
    }
    return summary;
}

} // namespace effectwalk

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

/// Whether the declaration says the operator writes the operand at `index`.
bool writesOperandAt(const GCOperBody &body, size_t index) {
    return camel::core::OperatorTable::instance().writesOperand(body.oper, false, index);
}

} // namespace

bool isWriteOfProgramState(const GCOperBody &body, size_t operandCount) {
    const auto &table = camel::core::OperatorTable::instance();
    // An ambient write is state outside the program whatever else the operator does; it is not a
    // write this judgement is about.
    if (table.touchesClass(body.oper, camel::core::sem::ResourceClass::Ambient, true)) {
        return false;
    }
    // A store into a cell or into an object the program owns. An assignment says so as a family
    // (`OperatorTable::isAssignment`) rather than through a resource declaration, which is why the
    // two are asked together; the operand clause below catches a store the declaration names on the
    // operand itself (a `var` parameter's cell).
    if (table.isAssignment(body.oper) || table.writesProgramState(body.oper)) {
        return true;
    }
    for (size_t index = 0; index < operandCount; ++index) {
        if (writesOperandAt(body, index)) {
            return true;
        }
    }
    return false;
}

} // namespace camel::runtime
