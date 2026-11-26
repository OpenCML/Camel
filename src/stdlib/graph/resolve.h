// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * One resolution of a handle, for every operator of the graph module that reads a graph or a node.
 *
 * A Node handle names either a node of a published graph or a node of the draft a transaction is
 * editing, and the two are read differently: the published node is in its Program's GCGraph, and
 * the draft's node is in the transaction that owns the draft (camel/runtime/draft.h). The read
 * operators learned that first (a rule condition reads the draft) and the fact operators did not,
 * so a draft node was answered as if it were a published node and every fact query on one said
 * `the handle names nothing`.
 *
 * The resolution lives here once, for both: a reader asks `viewOf` and reads through the view.
 * It is a resolution and not a reader -- `NodeView` answers the four things both representations
 * carry (kind, data type, data index, whether it has a payload), and a caller that needs more asks
 * the representation the view points at. Nothing is cached and nothing is derived here.
 *
 * A fact store has no anchor that names a draft (camel/runtime/facts.h, Anchor holds a GCGraph, and
 * a draft is not one), so a fact asked about a draft is answered by the draft itself at the
 * operator that asks, through this view. That is the reason every fact query of the module starts
 * from `viewOf` rather than from the store.
 */

#pragma once

#include "handle.h"

#include "camel/core/error/runtime.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"
#include "camel/runtime/program.h"

namespace camel::graphmod {

/// The graph a Graph or Node handle names; a fault when the Program holds no such graph.
inline const camel::runtime::GCGraph &graphOf(const Handle *handle, const char *site) {
    const auto *graph =
        handle->program() != nullptr ? handle->program()->graph(handle->graph()) : nullptr;
    if (graph == nullptr) {
        camel::core::error::throwRuntimeFault(
            camel::core::error::RuntimeDiag::InvalidArgument,
            site,
            "the graph is not in its program");
    }
    return *graph;
}

/// The node of a frozen graph; a fault when the graph has no such node or the node is dead.
inline const camel::runtime::GCNode &nodeOfFrozen(const Handle *handle, const char *site) {
    const camel::runtime::GCGraph &graph = graphOf(handle, site);
    const camel::runtime::GCNode *node =
        graph.containsNodeRef(handle->index()) ? graph.node(handle->index()) : nullptr;
    if (node == nullptr || node->kind == camel::runtime::GCNodeKind::Dead) {
        camel::core::error::throwRuntimeFault(
            camel::core::error::RuntimeDiag::InvalidArgument,
            site,
            "the node is not in its graph");
    }
    return *node;
}

/// Reads that belong to the frozen graphs only (a region, an operator, a transaction) go through
/// this name, which is the frozen reader: there is nothing of them in a draft.
inline const camel::runtime::GCNode &nodeOf(const Handle *handle, const char *site) {
    return nodeOfFrozen(handle, site);
}

/// A node of a frozen graph or of the draft being rewritten (a rule condition reads the draft).
/// Everything the read operators ask about a node is here; the rest of the file picks the source.
struct NodeView {
    const camel::runtime::GCNode *frozen                = nullptr;
    const camel::runtime::DraftNodeHeader *draftHeader  = nullptr;
    const camel::runtime::GraphTransaction *transaction = nullptr;
    camel::runtime::gc_node_ref_t ref                   = camel::runtime::kInvalidNodeRef;

    bool isDraft() const { return transaction != nullptr; }
    const auto &draft() const { return transaction->graph(); }
    camel::runtime::GCNodeKind kind() const { return isDraft() ? draftHeader->kind : frozen->kind; }
    type::Type *dataType() const { return isDraft() ? draftHeader->dataType : frozen->dataType(); }
    int64_t dataIndex() const { return isDraft() ? draftHeader->dataIndex : frozen->dataIndex; }
    bool hasPayload() const {
        return isDraft() ? !draft().payloadOf(ref).empty() : frozen->hasPayload();
    }
};

inline NodeView viewOf(const Handle *handle, const char *site) {
    NodeView view;
    // A node is in the draft exactly when its handle names the transaction that owns the draft:
    // there is no other way to be in a draft, and the handle keeps no draft pointer of its own.
    const auto *transaction = handle->transaction();
    if (transaction != nullptr) {
        view.transaction = transaction;
        view.ref         = handle->index();
        view.draftHeader = transaction->graph().header(view.ref);
        if (view.draftHeader == nullptr || !transaction->graph().alive(view.ref) ||
            view.draftHeader->kind == camel::runtime::GCNodeKind::Dead) {
            camel::core::error::throwRuntimeFault(
                camel::core::error::RuntimeDiag::InvalidArgument,
                site,
                "the node is not in its graph");
        }
        return view;
    }
    view.frozen = &nodeOfFrozen(handle, site);
    return view;
}

} // namespace camel::graphmod