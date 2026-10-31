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

    /// A transaction over a draft that already has an owner. It owns nothing: the draft outlives
    /// it or this dangles. It is opened where a draft exists but its owner is not a transaction at
    /// hand (the writings a caller builds from a draft alone), so that everything reading a draft
    /// still reads it through a transaction.
    static std::unique_ptr<GraphTransaction> borrow(GraphDraft &draft);

    /// A transaction on a graph that does not exist yet and whose identity is not known yet either:
    /// a tiles region is extracted before it is judged part of the program, and only an accepted
    /// region is attached and named. The transaction owns everything it makes; the id arrives with
    /// `assignId`, at attach, so the moment ids are allocated -- and with it the order of the graph
    /// numbers a run produces -- does not move.
    static std::unique_ptr<GraphTransaction> openPending();

    GraphTransaction(const GraphTransaction &)            = delete;
    GraphTransaction &operator=(const GraphTransaction &) = delete;

    /// Names the graph this transaction owns, at attach. On a transaction from `openPending`;
    /// calling it twice, or on any other transaction, is a compiler defect.
    void assignId(GraphId id);

    /// The graph being built or edited. The storage is the transaction's; the reference stays valid
    /// while the transaction lives, and every edit of the graph goes through it.
    GraphDraft &edit() { return graph_ != nullptr ? *graph_ : *borrowed_; }
    /// The graph as it stands: read-only, for a consumer that only inspects it.
    const GraphDraft &graph() const { return graph_ != nullptr ? *graph_ : *borrowed_; }
    /// The identity the graph carries (camel/runtime/graph_id.h). A graph that has not been named
    /// yet has no identity to give: reading one is a compiler defect, and says so in a Release
    /// build too -- a check that vanished there would let the graph be encoded under an id nobody
    /// assigned.
    GraphId id() const;

  private:
    explicit GraphTransaction(std::unique_ptr<GraphDraft> graph) : graph_(std::move(graph)) {}
    explicit GraphTransaction(GraphDraft &draft) : borrowed_(&draft) {}

    std::unique_ptr<GraphDraft> graph_;
    /// Set instead of `graph_` when the draft belongs to someone else: then this transaction only
    /// names it.
    GraphDraft *borrowed_ = nullptr;
    /// True between `openPending` and `assignId`: the graph has no identity yet, and `id()` reports
    /// a defect rather than hand out the zero an unnamed draft carries.
    bool pendingId_ = false;
};

} // namespace camel::runtime