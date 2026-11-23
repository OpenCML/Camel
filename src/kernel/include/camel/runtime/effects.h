// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The effect summary of a graph (docs/design/op-semantics 11.2, I20; 8.3): what running it may do
 * besides computing its value. A graph-level concept: the facts, the definition checks and the
 * loop accessors read it, none of which is about loops.
 */

#pragma once

#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

namespace camel::runtime {

/// What running a graph may do besides computing its value, through the graphs it calls and the
/// loops it runs (docs/design/op-semantics 11.2, I20). Computed from the nodes each time it is
/// asked for, so it cannot disagree with them.
struct EffectSummary {
    /// An operator with an effect outside the program's variables (the accesses its record
    /// states, united with the ones its function operand brings), or a call through a function
    /// value (what it runs is not known here).
    bool external = false;
    /// An operator that declares a write to a variable cell or to an object changed in place
    /// (its resource declaration: OperatorTable::writesProgramState).
    bool writesVariables = false;
    /// An operator that may fault (OperatorTraits::mayFault), or a call through a value.
    bool mayFault = false;
    /// A loop, or a call that may recurse: running it may not end.
    bool mayNotTerminate = false;

    EffectSummary &operator|=(const EffectSummary &other) {
        external |= other.external;
        writesVariables |= other.writesVariables;
        mayFault |= other.mayFault;
        mayNotTerminate |= other.mayNotTerminate;
        return *this;
    }
    bool pure() const { return !external && !writesVariables; }
    bool operator==(const EffectSummary &) const = default;
};

// The summary of a graph, of a region and of one node is computed in one place
// (`effect_walk.h`, private to the fact layer) and read everywhere as a fact: `core.effects` for a
// graph (`fact::graphEffects`), `core.nodeEffects` for a node, `core.regionEffects` for the regions
// a control node runs, `core.operatorEffects` for the effect of the operator a node runs. A reader
// that asked the walk directly would be a second entry to the same answer, which is why the walk's
// declarations are not in this header.
//
/// The effects of running the operator `oper` at `ref` of `graph`: the fault fact, what it writes,
/// and the union of the two halves of its access list (design principles 3.3). The declaration
/// gives the first half and it is fixed: an operator that states an access of its own keeps it
/// whatever the operands are, so one declared to reach outside the program is external at a node
/// even when its function operand is pure. The second half is what the function value arriving in
/// the declaration's function operand brings, read off that value's clause the way a call through
/// a value reads it, so an operator that states its accesses as its operand's is not external at a
/// node whose operand does not reach outside. Neither half narrows the other: the node's effect is
/// their union.
///
/// This is the one reading of an operator's effect, and the walk takes it at every node it visits
/// (`effect_walk.h`); `core.nodeEffects` and `core.operatorEffects` both ask it at the node they
/// are anchored on, which is why the two facts give one answer about that node.
EffectSummary effectSummaryOfOperator(core::OperId oper, const GCGraph *graph, gc_node_ref_t ref);

/// The declaration's half alone, where there is no node to read a function operand's clause off:
/// a node of a draft under rewrite, which no fact can be anchored on. It answers what the operator
/// states and, for the half an operand carries, the conservative "may reach outside and may not
/// end" -- never an answer narrower than the reading at a node.
EffectSummary declaredEffectsOfOperator(core::OperId oper);

} // namespace camel::runtime
