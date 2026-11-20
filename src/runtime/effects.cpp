// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The effect summary of a graph (camel/runtime/effects.h).
 */

#include "camel/runtime/effects.h"

#include "camel/runtime/effect_liveness.h"

#include "camel/core/operator_table.h"
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
/// accesses from there (design principles 3.3; author 2026-11-19): the clause of the function value
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

OperandClause functionOperandClause(const GCGraph &graph, gc_node_ref_t ref) {
    const auto &table = camel::core::OperatorTable::instance();
    const core::OperId oper = graph.nodeBodyAs<GCOperBody>(ref)->oper;
    const auto *facts = table.semantics(oper);
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
                summary.mayFault |= !traits || traits->mayFault;
                if (traits && traits->callsGraphs) {
                    // It runs a function the program hands it, and its accesses are that
                    // function's (design principles 3.3; author 2026-11-19): the same reading a
                    // call through a value follows reads the clause off the value's type, so a
                    // `map` over a pure function is not an external effect. What the declaration
                    // cannot narrow -- a type that writes no clause -- keeps the conservative
                    // reading, which is what `functionOperandClause` answers.
                    const OperandClause clause = functionOperandClause(*graph, ref);
                    summary.external |= clause.external;
                    summary.writesVariables |= clause.writesVariables;
                    summary.writesVariables |= table.writesProgramState(oper);
                    summary.mayNotTerminate = true;
                    break;
                }
                summary.external |= table.hasExternalEffect(oper);
                // What an operator writes is its resource declaration's (design principles 3.3).
                summary.writesVariables |= table.writesProgramState(oper);
                break;
            }
            case GCNodeKind::Call:
                // A call through a function value: its reach is the value's clause, read the same
                // way `Resolver::call` reads it for the footprint (design principles 3.3; author
                // 2026-11-20). `pure` is the clause that names nothing, so a call through a pure
                // value is not an external effect. This was the one reader in the tree that read no
                // type at all and answered "external" for every call, `pure` included, which is what
                // made a pure call look like a write nobody could name.
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

EffectSummary effectSummaryOf(const Program &program, const GCGraph *graph) {
    return EffectWalk{program}.of(graph);
}

EffectSummary effectSummaryOfRegion(const Program &program, const GCGraph *graph, uint32_t region) {
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
