// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Draft-level readings of `:op/keep`, beside the ones `std::opt` already writes: `replaceNode`
 * (opt/generic/rewrite.cpp) is the code under test, and the shape it acts on is built here by hand
 * so the two sides of the question can both be read -- where a keeper is due, and where none is.
 *
 * Where it is due: the removal takes the last read of a node whose running is observed, and a
 * division by zero is the one such node whose running is visible from outside -- the program faults
 * if and only if it ran. The value the program prints is routed through the node that takes the
 * division's place, so the run says the whole of it:
 *
 *   test::keep_node       KEEPNODE keep=<n> reads_the_divisor=<0|1> printed=<0|1>, and the program
 *                         faults on the division, which it can only do through the keeper
 *
 * Where none is due: a node that reads nothing observed, and a node whose observed source still has
 * another reader after the removal. Neither may get a keeper, and the program prints what it prints
 * without any pass:
 *
 *   test::keep_node_pure  KEEPNODE_PURE keep_pure=<n> keep_shared=<n>
 */

#include "camel/passes/test/keepnode.h"

#include "../opt/generic/rewrite.h"

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft_session.h"

#include <ostream>
#include <span>
#include <vector>

namespace {

using namespace camel::runtime;

using camel::core::OperId;
using camel::core::oper::Keep;
using camel::core::type::Type;

gc_node_ref_t constant(GraphTransaction &transaction, int64_t value) {
    return transaction.edit().materializeStaticValue(
        camel::core::rtdata::toSlot<int64_t>(value), Type::Int64());
}

gc_node_ref_t op(
    GraphTransaction &transaction, std::string_view uri,
    std::initializer_list<gc_node_ref_t> inputs) {
    auto &draft              = transaction.edit();
    const gc_node_ref_t node = draft.addOperNode(Type::Int64(), nullptr, uri);
    draft.setNormInputs(node, std::span<const gc_node_ref_t>(inputs.begin(), inputs.end()));
    return node;
}

/// The operator of a node, or kNoOper when it is not an operator: one place to read the payload.
/// It reads through the transaction and never names the draft type: the direct-connection ledger
/// (test/cases/runtime/graph_draft_ledger) counts the files that name it, and its numbers may only
/// fall.
OperId operOf(const GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft            = transaction.graph();
    const DraftNodeHeader *header = draft.header(id);
    if (header == nullptr || header->kind != GCNodeKind::Oper) {
        return camel::core::kNoOper;
    }
    const std::span<const std::byte> payload = draft.payloadOf(id);
    if (payload.size() < sizeof(GCOperBody)) {
        return camel::core::kNoOper;
    }
    return reinterpret_cast<const GCOperBody *>(payload.data())->oper;
}

/// The `:op/keep` nodes of the graph, by operator id: the count the plan asserts first.
size_t countKeeps(GraphTransaction &transaction) {
    auto &draft  = transaction.edit();
    size_t keeps = 0;
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (draft.alive(id) && operOf(transaction, id) == Keep) {
            ++keeps;
        }
    }
    return keeps;
}

/// The `:op/keep` node that reads `source`, or kInvalidNodeRef: the property asked of one node,
/// not the number of nodes in the graph.
gc_node_ref_t keeperOf(GraphTransaction &transaction, gc_node_ref_t source) {
    auto &draft = transaction.edit();
    for (gc_node_ref_t user : draft.normUsersOf(source)) {
        if (operOf(transaction, user) == Keep) {
            return user;
        }
    }
    return kInvalidNodeRef;
}

} // namespace

const camel::runtime::Program *
KeepNodeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    const std::vector<GCGraph *> graphs = session.collectReachableRuntimeGraphs();
    GCGraph *owner                      = nullptr;
    for (GCGraph *graph : graphs) {
        if (owner == nullptr || graph->nodeCount() > owner->nodeCount()) {
            owner = graph;
        }
    }
    if (owner == nullptr) {
        os << "KEEPNODE keep=0 reads_the_divisor=0 printed=0\n";
        return program;
    }

    auto &transaction = session.edit(owner);
    auto &draft       = transaction.edit();

    // ---- The print whose value this pass reroutes: the last `:io/println` operator, so the
    // reading is the program's own output and not a number the pass made up.
    gc_node_ref_t print = kInvalidNodeRef;
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        const OperId oper = operOf(transaction, id);
        if (oper != camel::core::kNoOper &&
            camel::core::OperatorTable::instance().uri(oper) == ":io/println") {
            print = id;
        }
    }
    const auto printed = print == kInvalidNodeRef ? std::vector<gc_node_ref_t>{}
                                                  : std::vector<gc_node_ref_t>(
                                                        draft.normInputsOf(print).begin(),
                                                        draft.normInputsOf(print).end());

    // ---- The shape, built by hand. The division divides by zero, so whether it runs is the
    // question; the node above it reads nothing else, so the replacement takes its last reader.
    const gc_node_ref_t one    = constant(transaction, 1);
    const gc_node_ref_t zero   = constant(transaction, 0);
    const gc_node_ref_t divide = op(transaction, ":op/div_i", {one, zero});
    const gc_node_ref_t reader = op(transaction, ":op/add_i", {divide, one});
    const size_t keepsBefore   = countKeeps(transaction);
    // What the print reads becomes the node that is replaced, so the run sees what the keep gives
    // back and the division is on the way to it.
    bool routed = false;
    if (!printed.empty()) {
        draft.replaceAllValueUses(printed.front(), reader);
        routed = true;
    }
    const gc_node_ref_t seven = constant(transaction, 7);
    camel::passes::generic::replaceNode(transaction, reader, seven);
    const size_t keeps          = countKeeps(transaction) - keepsBefore;
    const gc_node_ref_t keeper  = keeperOf(transaction, divide);
    const bool readsTheDivisor  = keeper != kInvalidNodeRef;

    os << "KEEPNODE keep=" << keeps << " reads_the_divisor=" << (readsTheDivisor ? 1 : 0)
       << " printed=" << (routed ? 1 : 0) << "\n";
    return session.commit();
}
const camel::runtime::Program *
KeepNodePureTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    const std::vector<GCGraph *> graphs = session.collectReachableRuntimeGraphs();
    GCGraph *owner                      = nullptr;
    for (GCGraph *graph : graphs) {
        if (owner == nullptr || graph->nodeCount() > owner->nodeCount()) {
            owner = graph;
        }
    }
    if (owner == nullptr) {
        os << "KEEPNODE_PURE keep_pure=0 keep_shared=0\n";
        return program;
    }

    auto &transaction = session.edit(owner);

    // ---- 1. A reader of nothing observed. The sum reads two constants, so no read goes out with
    // the removal that would take a running away: a keeper here would be an operator invented for a
    // node that has nothing to keep.
    const gc_node_ref_t three  = constant(transaction, 3);
    const gc_node_ref_t four   = constant(transaction, 4);
    const gc_node_ref_t sum    = op(transaction, ":op/add_i", {three, four});
    const gc_node_ref_t sevenA = constant(transaction, 7);
    const size_t beforePure    = countKeeps(transaction);
    camel::passes::generic::replaceNode(transaction, sum, sevenA);
    const size_t pure = countKeeps(transaction) - beforePure;

    // ---- 2. A reader of something observed, that the removal does not strand: the division may
    // fault, but it has a second reader standing, so it is read after the removal as it was before
    // it, and nothing has to be kept.
    const gc_node_ref_t five   = constant(transaction, 5);
    const gc_node_ref_t six    = constant(transaction, 6);
    const gc_node_ref_t divide = op(transaction, ":op/div_i", {five, six});
    const gc_node_ref_t gone   = op(transaction, ":op/add_i", {divide, five});
    op(transaction, ":op/add_i", {divide, six});
    const gc_node_ref_t sevenB = constant(transaction, 7);
    const size_t beforeShared  = countKeeps(transaction);
    camel::passes::generic::replaceNode(transaction, gone, sevenB);
    const size_t shared = countKeeps(transaction) - beforeShared;

    os << "KEEPNODE_PURE keep_pure=" << pure << " keep_shared=" << shared << "\n";
    return session.commit();
}