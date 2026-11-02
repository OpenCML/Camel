// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Small accessors over the draft nodes the GIR builder works on, shared by the source files
 * the builder is split into (builder.cpp, program.cpp, captures.cpp). Internal to the
 * builder: nothing outside src/compile/gir includes this header.
 */

#pragma once

#include "camel/compile/gct.h"
#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/types.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/utils/assert.h"

#include <format>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>

namespace camel::compile::gir {

using compile_graph_ptr_t = std::shared_ptr<DraftGraphBuilder>;

/// The name a call writes for its callee: the `DREF` under a generic application (`TAPP`) or a
/// `with` application (`WITH`). Empty when the callee is an expression (a function value).
/// The shapes of a `match type` case that a type has besides struct and tuple: the shape bits of
/// `GCT::typeCaseBit` for an array, a function, a `T?`, a `T!`, a map and a set (an ordinary tuple
/// and a struct are told apart by the callers, which also walk their fields). A type that has one
/// is still a leaf for `#leaf`.
inline uint16_t extraTypeShapes(const camel::core::type::Type *type) {
    using camel::core::type::TypeCode;
    using Kind = GCT::TypeCaseKind;
    if (type == nullptr) {
        return 0;
    }
    switch (type->code()) {
    case TypeCode::Array:
        return GCT::typeCaseBit(Kind::Array);
    case TypeCode::Function:
        return GCT::typeCaseBit(Kind::Func);
    case TypeCode::Tuple: {
        const auto *tuple = static_cast<const camel::core::type::TupleType *>(type);
        if (tuple->isNullable()) {
            return GCT::typeCaseBit(Kind::Nullable);
        }
        return tuple->isFallible() ? GCT::typeCaseBit(Kind::Fallible) : 0;
    }
    default:
        break;
    }
    if (const auto *collection = camel::core::type::asCollectionType(type)) {
        return GCT::typeCaseBit(collection->isMap() ? Kind::Map : Kind::Set);
    }
    return 0;
}

inline std::string writtenCalleeName(const GCT::node_ptr_t &callee) {
    GCT::node_ptr_t node = callee;
    while (node != nullptr) {
        if (node->type() == GCT::NodeKind::WITH || node->type() == GCT::NodeKind::TAPP) {
            node = node->at(0);
            continue;
        }
        return node->type() == GCT::NodeKind::DREF ? node->name() : std::string{};
    }
    return {};
}

/// The name a `LINK` writes for its callee; empty for an expression.
inline std::string writtenCallName(const GCT::node_ptr_t &call) {
    return call != nullptr && call->type() == GCT::NodeKind::LINK ? writtenCalleeName(call->at(0))
                                                                  : std::string{};
}

inline runtime::DraftEdgeKind toDraftEdgeKind(LinkType type) {
    switch (type) {
    case LinkType::Norm:
    case LinkType::With:
        return runtime::DraftEdgeKind::Data;
    case LinkType::Ctrl:
        return runtime::DraftEdgeKind::Ctrl;
    case LinkType::Order:
        return runtime::DraftEdgeKind::Order;
    case LinkType::Sync:
        return runtime::DraftEdgeKind::Sync;
    }
    throw std::runtime_error("Unknown link type.");
    return runtime::DraftEdgeKind::Data;
}

/// The role of the port a link makes: `with` is the context role, everything else the ordinary
/// one (the capture role is the callee's capture table's, not a `LinkType`).
inline runtime::PortRole toPortRole(LinkType type) {
    return type == LinkType::With ? runtime::PortRole::Context : runtime::PortRole::Normal;
}

/// The draft a handle names, once the handle is known to name it. An address says where a draft
/// lives, not which draft it is: the allocator hands a destroyed draft's memory to the next one, so
/// a handle whose draft is gone would otherwise read that other graph's node at the same id and
/// hand back a plausible value of the wrong graph. The epoch the handle carries says which draft it
/// was made in, which is what makes it safe to dereference the address it holds.
inline runtime::GraphDraft *nodeDraftOf(node_handle_t node) {
    GUARD(node != nullptr, "Draft node is null.");
    GUARD(
        node.epoch == node.draft->epoch(),
        std::format(
            "A draft node names the draft it was made in, and every draft is made with an epoch of "
            "its own: this handle was made in epoch {} at {} and that address now holds epoch {}, "
            "so the handle outlived its graph.",
            node.epoch,
            static_cast<const void *>(node.draft),
            node.draft->epoch()));
    return node.draft;
}

inline runtime::GCNodeKind nodeKindOf(node_handle_t node) {
    GUARD(node != nullptr, "Draft node is null.");
    return nodeDraftOf(node)->header(node.id)->kind;
}

inline camel::core::type::Type *nodeTypeOf(node_handle_t node) {
    GUARD(node != nullptr, "Draft node is null.");
    return nodeDraftOf(node)->header(node.id)->dataType;
}

inline const runtime::DraftNodeHeader *nodeHeaderOf(node_handle_t node) {
    GUARD(node != nullptr, "Draft node is null.");
    return nodeDraftOf(node)->header(node.id);
}

inline DraftGraphBuilder *nodeGraphOf(node_handle_t node) {
    // nodeDraftOf states the identity of the handle first, so that a handle that outlived its graph
    // is reported as that and not as a graph that has no builder.
    (void)nodeDraftOf(node);
    auto *builder = DraftGraphBuilder::fromRef(node);
    GUARD(builder != nullptr, "Draft node is not owned by a compile graph builder.");
    return builder;
}

/// The transaction the node's graph is built in: the owner of what `nodeDraftOf` points at, and the
/// identity two nodes of one graph agree on (the storage's address says where it happens to live).
inline runtime::GraphTransaction *nodeTransactionOf(node_handle_t node) {
    return &nodeGraphOf(node)->transaction();
}

inline draft_node_ref_t nodeIdOf(node_handle_t node) {
    GUARD(node != nullptr, "Draft node is null.");
    return node.id;
}

inline std::span<const draft_node_ref_t> normInputsOf(node_handle_t node) {
    return nodeDraftOf(node)->normInputsOf(node.id);
}

inline std::span<const draft_node_ref_t> withInputsOf(node_handle_t node) {
    return nodeDraftOf(node)->withInputsOf(node.id);
}

inline std::span<const draft_node_ref_t> ctrlInputsOf(node_handle_t node) {
    return nodeDraftOf(node)->ctrlInputsOf(node.id);
}

/// The statement this one is written directly after in the same `sync` block, if any
/// (docs/design/effects/edges.cn.md 1.5).
inline std::span<const draft_node_ref_t> syncInputsOf(node_handle_t node) {
    return nodeDraftOf(node)->syncInputsOf(node.id);
}

inline std::span<const draft_node_ref_t> normUsersOf(node_handle_t node) {
    return nodeDraftOf(node)->normUsersOf(node.id);
}

inline std::span<const draft_node_ref_t> withUsersOf(node_handle_t node) {
    return nodeDraftOf(node)->withUsersOf(node.id);
}

inline std::span<const draft_node_ref_t> ctrlUsersOf(node_handle_t node) {
    return nodeDraftOf(node)->ctrlUsersOf(node.id);
}

inline bool nodeIsKind(node_handle_t node, runtime::GCNodeKind kind) {
    return node != nullptr && nodeKindOf(node) == kind;
}


inline bool isMacroGraph(const compile_graph_ptr_t &graph) {
    return graph && graph->view().isMacroGraph();
}

inline bool sameGraph(node_handle_t lhs, const compile_graph_ptr_t &graph) {
    return lhs != nullptr && nodeGraphOf(lhs) == graph.get();
}

inline void linkNodes(LinkType type, node_handle_t from, node_handle_t to) {
    GUARD(from != nullptr && to != nullptr, "Cannot link null draft nodes.");
    GUARD(
        nodeGraphOf(from) == nodeGraphOf(to),
        "Compile-time link must stay within one graph: an edge belongs to the graph that owns "
        "both of its ends, and a link into another graph outlives the graph it was built in");
    nodeGraphOf(from)->link(toDraftEdgeKind(type), from, to, toPortRole(type));
}

inline void tryRemoveCtrlLink(node_handle_t from, node_handle_t to) {
    // if from has already linked to to by a ctrl link, remove it first
    // sometimes we may need to change a ctrl link (linked before) to a data link
    // because data link has higher priority than ctrl link
    // and we don't want to have duplicate links
    if (!from || !to || nodeGraphOf(from) != nodeGraphOf(to)) {
        return;
    }
    (void)nodeGraphOf(from)->unlink(runtime::DraftEdgeKind::Ctrl, from, to);
}

inline bool linkCheek(node_handle_t from, node_handle_t to) {
    // prevent linking a node to itself
    if (!from || !to || from == to) {
        return false;
    }
    return true;
}

} // namespace camel::compile::gir
