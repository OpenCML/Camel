// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/runtime/graph_txn.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/messages/internal.h"

namespace camel::runtime {

namespace {

/// A graph read before it is named would be encoded under an id nobody assigned, and two graphs
/// could then carry the same number. That is a defect of the compiler, not of the program, and it
/// is reported in a Release build too: an ASSERT here would leave the build that ships without the
/// check.
[[noreturn]] void reportUnnamedGraph() {
    throw camel::core::error::DiagnosticBuilder::of(
              camel::core::error::InternalDiag::UnknownInternalError)
        .commit("A graph transaction's id was read before the graph was named (openPending "
                "without assignId).");
}

} // namespace

std::unique_ptr<GraphTransaction>
GraphTransaction::open(GraphId id, camel::core::type::FunctionType *funcType) {
    auto graph = std::make_unique<GraphDraft>();
    graph->setFuncType(funcType);
    graph->setId(id);
    return std::unique_ptr<GraphTransaction>(new GraphTransaction(std::move(graph)));
}

std::unique_ptr<GraphTransaction> GraphTransaction::openPending() {
    auto transaction =
        std::unique_ptr<GraphTransaction>(new GraphTransaction(std::make_unique<GraphDraft>()));
    transaction->pendingId_ = true;
    return transaction;
}

void GraphTransaction::assignId(GraphId id) {
    if (!pendingId_ || graph_ == nullptr) {
        throw camel::core::error::DiagnosticBuilder::of(
                  camel::core::error::InternalDiag::UnknownInternalError)
            .commit("assignId was called on a graph transaction that had no id pending.");
    }
    graph_->setId(id);
    pendingId_ = false;
}

GraphId GraphTransaction::id() const {
    if (pendingId_) {
        reportUnnamedGraph();
    }
    return graph().id();
}

std::unique_ptr<GraphTransaction>
GraphTransaction::decode(const Program &program, const GCGraph *graph) {
    return std::unique_ptr<GraphTransaction>(
        new GraphTransaction(GraphDraft::decode(program, graph)));
}

std::unique_ptr<GraphTransaction> GraphTransaction::borrow(GraphDraft &draft) {
    return std::unique_ptr<GraphTransaction>(new GraphTransaction(draft));
}

} // namespace camel::runtime