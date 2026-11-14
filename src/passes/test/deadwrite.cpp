// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The judgement `camel::runtime::deadWrite` makes, printed for a plan to pin
 * (`camel/runtime/effect_liveness.h`). Every graph of the program is walked, in node index order,
 * and every node whose declaration says it writes a place of the program prints one line:
 *
 *   DEADWRITE graph=<name> node=<ref> oper=<uri> rooted=<0|1> read=<0|1> dead=<0|1>
 *
 * The program is passed on unchanged: this is an inspection, not a rewrite.
 */

#include "camel/passes/test/deadwrite.h"

#include "camel/core/operator_table.h"
#include "camel/runtime/effect_liveness.h"
#include "passes/opt/generic/rewrite.h"

#include <algorithm>
#include <span>

const camel::runtime::Program *
DeadWriteTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    const auto &table = camel::core::OperatorTable::instance();
    for (const GCGraph *graph : program->graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (uint32_t index = 0; index < graph->nodeCount(); ++index) {
            const gc_node_ref_t ref = static_cast<gc_node_ref_t>(index);
            const GCNode *node      = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto *body = graph->nodeBodyAs<GCOperBody>(ref);
            if (body == nullptr) {
                continue;
            }
            // A node that writes nothing is not what this pass reports: the judgement is about a
            // write, and printing every operation would bury the ones it is about.
            bool writes       = table.hasExternalEffect(body->oper);
            const auto inputs = graph->normInputsOf(ref);
            for (size_t port = 0; port < inputs.size() && !writes; ++port) {
                writes = table.writesOperand(body->oper, false, port);
            }
            if (!writes) {
                continue;
            }
            os << "DEADWRITE graph=" << graph->name() << " node=" << ref << " oper=" << body->uri()
               << " rooted=" << (writeIsRooted(*graph, ref) ? 1 : 0)
               << " read=" << (valueIsRead(*graph, ref) ? 1 : 0)
               << " dead=" << (deadWrite(*graph, ref) ? 1 : 0) << "\n";
        }
    }
    // Every aggregate, and what the judgement says about its last member: a refusal that cannot say
    // which guard refused cannot be told from a guard written wrong.
    //
    // `members=` is the aggregate's slot table against the `with` inputs that fill it, position by
    // position -- `<component>:<kind of the node that fills it>`. It is *not* a list of the node's
    // normal inputs: an aggregate of two members has one normal input (its template) and two `with`
    // inputs, so a reading that counted the normal inputs printed `inputs=1` for `arity=2` and said
    // the members were something they were not. `components=` is the tuple type's size, which is the
    // number that says which component is the last one; a component the table does not name keeps the
    // value the template carries.
    for (const GCGraph *graph : program->graphs()) {
        for (uint32_t index = 0; index < graph->nodeCount(); ++index) {
            const gc_node_ref_t ref = static_cast<gc_node_ref_t>(index);
            if (camel::runtime::aggregateArity(*graph, ref) == 0) {
                continue;
            }
            const auto *body = graph->nodeBodyAs<camel::runtime::GCFillBody>(ref);
            const auto slots = body != nullptr ? body->slots() : std::span<const gc_slot_idx_t>{};
            const auto values = graph->withInputsOf(ref);
            const size_t paired = std::min(slots.size(), values.size());
            os << "AGGREGATE graph=" << graph->name() << " node=" << ref
               << " components=" << camel::runtime::aggregateComponentCount(*graph, ref)
               << " slots=" << slots.size() << " norm=" << graph->normInputsOf(ref).size()
               << " with=" << values.size() << " members=";
            for (size_t position = 0; position < paired; ++position) {
                const GCNode *producer = graph->node(values[position]);
                os << slots[position] << ":"
                   << (producer == nullptr ? -1 : static_cast<int>(producer->kind)) << ",";
            }
            const uint32_t tail = camel::runtime::aggregateTailInputIndex(*graph, ref);
            os << " tail="
               << (tail == camel::runtime::kInvalidNodeRef
                       ? -1
                       : static_cast<int>(values[tail]))
               << " verdict="
               << camel::runtime::aggregateTailVerdictName(
                      camel::runtime::aggregateTailVerdict(*graph, ref))
               << "\n";
        }
        // A closure, and every kind of user it still has: whether the sweep may take it is a
        // question about these counts, so the count that blocked it has to be readable.
        for (uint32_t index = 0; index < graph->nodeCount(); ++index) {
            const gc_node_ref_t ref = static_cast<gc_node_ref_t>(index);
            if (graph->node(ref) == nullptr || graph->node(ref)->kind != GCNodeKind::Func) {
                continue;
            }
            bool named = false;
            for (gc_node_ref_t closure : graph->closureNodes()) {
                named = named || closure == ref;
            }
            os << "FUNC graph=" << graph->name() << " node=" << ref
               << " normUsers=" << graph->normUsersOf(ref).size()
               << " withUsers=" << graph->withUsersOf(ref).size()
               << " ctrl=" << (graph->ctrlInputsOf(ref).size() + graph->ctrlUsersOf(ref).size())
               << " order=" << (graph->orderPredsOf(ref).size() + graph->orderUsersOf(ref).size())
               << " sync=" << (graph->syncPredsOf(ref).size() + graph->syncUsersOf(ref).size())
               << " closureNode=" << (named ? 1 : 0)
               << " unreadClosure=" << (camel::runtime::isUnreadClosure(*graph, ref) ? 1 : 0)
               << "\n";
        }
    }
    // What the sweep took, by kind: the count that says extending the kinds it takes is doing work
    // rather than leaving every graph it was written for alone (an assertion about one case can say
    // "the closure is gone", never how many nodes in all went).
    os << "SWEEP func=" << camel::passes::generic::sweepRemovalsOf(camel::runtime::GCNodeKind::Func)
       << " call=" << camel::passes::generic::sweepRemovalsOf(camel::runtime::GCNodeKind::Call)
       << " gate=" << camel::passes::generic::sweepRemovalsOf(camel::runtime::GCNodeKind::Gate)
       << " oper=" << camel::passes::generic::sweepRemovalsOf(camel::runtime::GCNodeKind::Oper)
       << "\n";
    return program;
}