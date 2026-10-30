// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The transaction a graph is built or edited in.
 *
 * A graph is not owned by whoever fills it: its editable form belongs to a transaction, and the
 * transaction is the handle to it. The front end opens one when it starts a graph and builds in
 * it (createCompileGraph); a rewrite opens one on a version it is about to change. Where the
 * storage lives and how a committed graph is laid out can then change behind this handle alone,
 * without every consumer of a graph changing with it.
 */

#pragma once

#include "camel/runtime/draft.h"

#include <memory>

namespace camel::runtime {

class GraphTransaction {
  public:
    /// A transaction on a graph that does not exist yet, `id` being the identity it will carry:
    /// what the front end opens for a graph it is about to build.
    static std::unique_ptr<GraphTransaction>
    open(GraphId id, camel::core::type::FunctionType *funcType = nullptr);

    /// A transaction on `graph`, a version of `program`: the graph's editable form is decoded from
    /// it and stands for it while the transaction is open.
    static std::unique_ptr<GraphTransaction> decode(const Program &program, const GCGraph *graph);

    GraphTransaction(const GraphTransaction &)            = delete;
    GraphTransaction &operator=(const GraphTransaction &) = delete;

    /// The graph being built or edited. The storage is the transaction's; the reference stays valid
    /// while the transaction lives, and every edit of the graph goes through it.
    GraphDraft &edit() { return *graph_; }
    /// The graph as it stands: read-only, for a consumer that only inspects it.
    const GraphDraft &graph() const { return *graph_; }
    /// The identity the graph carries (camel/runtime/graph_id.h).
    GraphId id() const { return graph_->id(); }

  private:
    explicit GraphTransaction(std::unique_ptr<GraphDraft> graph) : graph_(std::move(graph)) {}

    std::unique_ptr<GraphDraft> graph_;
};

} // namespace camel::runtime