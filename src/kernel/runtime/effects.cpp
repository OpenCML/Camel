// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The effect summary of a graph (camel/runtime/effects.h): the value, the operator clause, and the
 * one computation of it that the fact layer registers (`effect_walk.h`).
 */

#include "camel/runtime/effects.h"

#include "effect_walk.h"

#include "effect_walk_detail.h"

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

namespace effectwalk {

template <class Reader> EffectSummary ofGraph(Reader reader, const GCGraph *graph) {
    return detail::EffectWalkT<Reader>{std::move(reader)}.of(graph);
}

template <class Reader>
EffectSummary ofRegion(Reader reader, const GCGraph *graph, uint32_t region) {
    if (graph == nullptr || region == 0) {
        return {};
    }
    detail::EffectWalkT<Reader> walk{std::move(reader)};
    walk.active.insert(graph);
    return walk.ofNodes(graph, region);
}

EffectSummary ofGraph(const Program &program, const GCGraph *graph) {
    return ofGraph(detail::ProgramReader{program}, graph);
}

EffectSummary ofRegion(const Program &program, const GCGraph *graph, uint32_t region) {
    return ofRegion(detail::ProgramReader{program}, graph, region);
}

} // namespace effectwalk

EffectSummary
effectSummaryOfOperator(camel::core::OperId oper, const GCGraph *graph, gc_node_ref_t ref) {
    // The same reading the walk asks through its reader (design principles 3.3): the operator
    // table's half and, when the declaration takes its accesses from a function operand, that
    // operand's clause. This signature is what `graph.is_pure` asks, so it is unchanged; it brings
    // the reader that has no program because the reading needs none.
    return effectwalk::detail::effectSummaryOfOperatorT(
        oper,
        effectwalk::detail::NodeOperandReader{},
        graph,
        ref);
}

EffectSummary declaredEffectsOfOperator(camel::core::OperId oper) {
    // A reader with no node to read a function operand's clause off (a draft under rewrite).
    return effectSummaryOfOperator(oper, nullptr, 0);
}

namespace effectwalk {

template <class Reader>
EffectSummary ofNode(Reader reader, const GCGraph *graph, gc_node_ref_t ref) {
    EffectSummary summary;
    if (graph == nullptr || !reader.hasNode(graph, ref)) {
        return summary;
    }
    switch (reader.nodeKind(graph, ref)) {
    case GCNodeKind::Oper:
        // The one reading of an operator's effect, at this node (`effectSummaryOfOperator`).
        summary |= reader.operatorEffect(graph, ref);
        break;
    case GCNodeKind::Call:
        // A call through a function value: its reach is the value's clause (design principles 3.3),
        // and nothing says what it runs may end or cannot fault.
        summary.external |= reader.callClauseOf(*graph, ref).external;
        summary.mayFault        = true;
        summary.mayNotTerminate = true;
        break;
    case GCNodeKind::Func:
        return ofGraph(reader, reader.calleeOf(*graph, ref));
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

EffectSummary ofNode(const Program &program, const GCGraph *graph, gc_node_ref_t ref) {
    return ofNode(detail::ProgramReader{program}, graph, ref);
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
