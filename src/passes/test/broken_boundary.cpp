// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The two passes of broken_boundary.h: each writes one thing a pass may not do and returns the
 * program, so the check every pass ends with has something to refuse. What they print is what the
 * plan compares: the pass ran, and the graph it handed back is the one the guard names.
 */

#include "camel/passes/test/broken_boundary.h"

#include "camel/core/context/context.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph_txn.h"

#include <vector>

using namespace camel::runtime;

const camel::runtime::Program *
BreakExitTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    GCGraph *root = program->root();
    if (root == nullptr) {
        os << "test::break_exit: no root graph\n";
        return program;
    }
    const gc_node_ref_t exit = root->exitNodeRef();
    if (exit == kInvalidNodeRef) {
        os << "test::break_exit: the root graph has no exit node\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(root);
    transaction.edit().eraseNode(exit);
    os << "test::break_exit: erased exit node " << static_cast<unsigned>(exit) << '\n';
    return session.commit();
}

const camel::runtime::Program *
BreakPortsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    GCGraph *root = program->root();
    if (root == nullptr) {
        os << "test::break_ports: no root graph\n";
        return program;
    }
    // The first operator node of any graph the program has gets its first ordinary port pointed at a
    // node of no type: the port is live, so the graph encodes, and the call it stands for is one
    // nothing can run. The root graph of a whole program is a wrapper around the entry function,
    // so the operator is in the graph of the function itself.
    GCGraph *targetGraph = nullptr;
    gc_node_ref_t target = kInvalidNodeRef;
    for (GCGraph *graph : program->graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const GCNode *record = graph->node(it.ref());
            if (record != nullptr && record->kind == GCNodeKind::Oper) {
                targetGraph = graph;
                target      = it.ref();
                break;
            }
        }
        if (target != kInvalidNodeRef) {
            break;
        }
    }
    if (target == kInvalidNodeRef) {
        os << "test::break_ports: the program has no operator node\n";
        return program;
    }
    const std::span<const gc_node_ref_t> given = targetGraph->normInputsOf(target);
    std::vector<gc_node_ref_t> ports(given.begin(), given.end());
    if (ports.empty()) {
        os << "test::break_ports: operator node " << static_cast<unsigned>(target) << " has no ports\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(targetGraph);
    auto &draft       = transaction.edit();
    const gc_node_ref_t untyped = draft.addDataNode(nullptr, 0);
    ports[0]                    = untyped;
    draft.setNormInputs(target, ports);
    os << "test::break_ports: operator node " << static_cast<unsigned>(target)
       << " port 0 now reads untyped node " << static_cast<unsigned>(untyped) << '\n';
    return session.commit();
}

const camel::runtime::Program *
DanglePortTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    GCGraph *targetGraph = nullptr;
    gc_node_ref_t target = kInvalidNodeRef;
    for (GCGraph *graph : program->graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const GCNode *record = graph->node(it.ref());
            if (record != nullptr && record->kind == GCNodeKind::Oper) {
                targetGraph = graph;
                target      = it.ref();
                break;
            }
        }
        if (target != kInvalidNodeRef) {
            break;
        }
    }
    if (target == kInvalidNodeRef) {
        os << "test::dangle_port: the program has no operator node\n";
        return program;
    }
    const std::span<const gc_node_ref_t> given = targetGraph->normInputsOf(target);
    std::vector<gc_node_ref_t> ports(given.begin(), given.end());
    if (ports.empty()) {
        os << "test::dangle_port: operator node " << static_cast<unsigned>(target) << " has no ports\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(targetGraph);
    ports[0]          = kInvalidNodeRef;
    transaction.edit().setNormInputs(target, ports);
    os << "test::dangle_port: operator node " << static_cast<unsigned>(target)
       << " port 0 now reads a node the graph does not have\n";
    return session.commit();
}

const camel::runtime::Program *FailsWithValueTestPass::apply(
    const camel::runtime::Program *program, std::ostream &os) {
    os << "test::fails_with_value: ending with an error value instead of a program\n";
    failWithErrorValue(camel::core::rtdata::makeError(
        "TestFailure", "this test pass ended with an error value", "<test::fails_with_value>"));
    return nullptr;
}

const camel::runtime::Program *
BreakJoinTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program->root() == nullptr) {
        os << "test::break_join: no root graph\n";
        return program;
    }
    for (GCGraph *graph : program->graphs()) {
        // A branch, and a node of the same draft that is not a join: the draft is allowed to be
        // inconsistent, and the encoder is the place that refuses it.
        gc_node_ref_t brch = kInvalidNodeRef;
        gc_node_ref_t other = kInvalidNodeRef;
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if ((*it)->kind != GCNodeKind::Brch || brch != kInvalidNodeRef) {
                continue;
            }
            brch = it.ref();
        }
        if (brch == kInvalidNodeRef) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if (it.ref() != brch && (*it)->kind != GCNodeKind::Join) {
                other = it.ref();
                break;
            }
        }
        if (other == kInvalidNodeRef) {
            continue;
        }
        RuntimeGraphDraftSession session(context_, program);
        auto &transaction = session.edit(graph);
        auto &draft       = transaction.edit();
        draft.setBrchJoin(brch, other);
        os << "test::break_join: branch " << static_cast<unsigned>(brch) << " now names node "
           << static_cast<unsigned>(other) << ", which is not a join\n";
        return session.commit();
    }
    os << "test::break_join: the program has no branch node\n";
    return program;
}
