/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Small accessors over the draft nodes the GIR builder works on, shared by the source files
 * the builder is split into (builder.cpp, program.cpp, captures.cpp). Internal to the
 * builder: nothing outside src/compile/gir includes this header.
 */

#pragma once

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/types.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft.h"
#include "camel/utils/assert.h"

#include <span>
#include <string_view>

namespace camel::compile::gir {

using compile_graph_ptr_t = std::shared_ptr<DraftGraphBuilder>;

inline runtime::DraftEdgeKind toDraftEdgeKind(LinkType type) {
    switch (type) {
    case LinkType::Norm:
        return runtime::DraftEdgeKind::Norm;
    case LinkType::With:
        return runtime::DraftEdgeKind::With;
    case LinkType::Ctrl:
        return runtime::DraftEdgeKind::Ctrl;
    }
    ASSERT(false, "Unknown link type.");
    return runtime::DraftEdgeKind::Norm;
}

inline runtime::GCNodeKind nodeKindOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
    return node.draft->header(node.id)->kind;
}

inline camel::core::type::Type *nodeTypeOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
    return node.draft->header(node.id)->dataType;
}

inline const runtime::DraftNodeHeader *nodeHeaderOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
    return node.draft->header(node.id);
}

inline runtime::GraphDraft *nodeDraftOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
    return node.draft;
}

inline DraftGraphBuilder *nodeGraphOf(node_handle_t node) {
    auto *builder = DraftGraphBuilder::fromDraft(nodeDraftOf(node));
    ASSERT(builder != nullptr, "Draft node is not owned by a compile graph builder.");
    return builder;
}

inline draft_node_ref_t nodeIdOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
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
    return graph && graph->funcType() && graph->funcType()->modifiers().macro();
}

inline bool sameGraph(node_handle_t lhs, const compile_graph_ptr_t &graph) {
    return lhs != nullptr && nodeGraphOf(lhs) == graph.get();
}

inline void linkNodes(LinkType type, node_handle_t from, node_handle_t to) {
    ASSERT(from != nullptr && to != nullptr, "Cannot link null draft nodes.");
    ASSERT(nodeGraphOf(from) == nodeGraphOf(to), "Compile-time link must stay within one graph.");
    nodeGraphOf(from)->link(toDraftEdgeKind(type), from, to);
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
