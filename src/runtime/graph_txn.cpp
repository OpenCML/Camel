// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/runtime/graph_txn.h"

namespace camel::runtime {

std::unique_ptr<GraphTransaction>
GraphTransaction::open(GraphId id, camel::core::type::FunctionType *funcType) {
    auto graph = std::make_unique<GraphDraft>();
    graph->setFuncType(funcType);
    // The front end places every node itself, so a read across the border of an arm is left for
    // the validator to report rather than moving the reader into the arm.
    graph->setInheritsRegions(false);
    graph->setId(id);
    return std::unique_ptr<GraphTransaction>(new GraphTransaction(std::move(graph)));
}

} // namespace camel::runtime