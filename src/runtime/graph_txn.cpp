// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/runtime/graph_txn.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/messages/internal.h"

namespace camel::runtime {

std::unique_ptr<GraphTransaction>
GraphTransaction::open(GraphId id, camel::core::type::FunctionType *funcType) {
    auto graph = std::make_unique<GraphDraft>();
    graph->setFuncType(funcType);
    graph->setId(id);
    return std::unique_ptr<GraphTransaction>(new GraphTransaction(std::move(graph)));
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