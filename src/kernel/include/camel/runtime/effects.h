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
    /// An operator with an effect outside the program's variables (OperatorTable::
    /// hasExternalEffect), or a call through a function value (what it runs is not known here).
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
// a control node runs, `core.operatorEffects` for the clause of the operator a node runs. A reader
// that asked the walk directly would be a second entry to the same answer, which is why the walk's
// declarations are not in this header.
//
/// The effects of running the operator `oper` itself, without the node that calls it: the clause
/// its record states and the fault fact, which is the answer a reader of the declaration asks for
/// -- `graph.is_pure`, a listing -- and the same one a node of it starts from. An operator that
/// takes its accesses from a function operand is read conservatively here: what the operand is is
/// the node's to say, and `core.nodeEffects` is where it says it.
EffectSummary effectSummaryOfOperator(camel::core::OperId oper);

} // namespace camel::runtime
