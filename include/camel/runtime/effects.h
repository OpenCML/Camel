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

/// The effects of running `graph`, a graph of `program`, once (nullptr: none).
EffectSummary effectSummaryOf(const Program &program, const GCGraph *graph);
/// The effects of running the nodes of `region` of `graph` (a LoopCond or LoopBody region, the
/// regions inside it included) once.
EffectSummary
effectSummaryOfRegion(const Program &program, const GCGraph *graph, uint32_t region);

} // namespace camel::runtime
