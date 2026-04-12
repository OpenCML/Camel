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
 * Created: Apr. 12, 2026
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Compile-time graph host services.
 *
 * This object owns the mutable node-construction services that should
 * eventually outlive the old `Graph` wrapper:
 * - arena-backed node ownership
 * - runtime/static slot numbering
 * - node cold metadata
 * - compile-time operator registry
 *
 * The current refactor stage keeps relationship/topology containers on
 * `Graph`, but moves node-host responsibilities here so nodes stop depending
 * directly on the legacy graph wrapper for basic storage services.
 */

#pragma once

#include "camel/compile/gir/arena.h"
#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/types.h"
#include "camel/core/data.h"

#include <memory>
#include <string>
#include <unordered_map>

class OperatorIndex;

namespace camel::compile::gir {

class Graph;
class Node;

using data_ptr_t        = camel::core::data::data_ptr_t;
using static_slot_vec_t = std::vector<slot_t>;

class GraphBuilder {
  public:
    explicit GraphBuilder(Graph &owner);

    GraphBuilder(const GraphBuilder &)            = delete;
    GraphBuilder &operator=(const GraphBuilder &) = delete;
    GraphBuilder(GraphBuilder &&)                 = delete;
    GraphBuilder &operator=(GraphBuilder &&)      = delete;

    Graph &owner() const { return owner_; }
    graph_arena_ptr_t arena() const { return draftBuilder_->arena(); }
    DraftGraphBuilder &draftBuilder() const { return *draftBuilder_; }
    runtime::GraphDraft &draft() const { return draftBuilder_->draft(); }

    Node *ownNode(Node *node);
    void bindDraftNode(Node *node, runtime::gc_node_ref_t draftId);
    runtime::gc_node_ref_t draftNodeId(const Node *node) const;
    runtime::gc_node_ref_t tryDraftNodeId(const Node *node) const;
    bool hasDraftNode(const Node *node) const;
    void eraseDraftNode(const Node *node);

    data_idx_t addStaticSlot(slot_t slot);
    data_idx_t addStaticData(const data_ptr_t &data);
    data_idx_t addRuntimeData();
    void setStaticSlot(data_idx_t index, slot_t slot);
    void setStaticData(data_idx_t index, const data_ptr_t &data);
    slot_t getStaticDataSlot(data_idx_t index) const;

    const static_slot_vec_t &staticDataArr() const { return staticDataArr_; }
    size_t staticDataSize() const;
    size_t runtimeDataSize() const { return runtimeDataSize_; }
    bool hasPackedStaticData() const { return hasPackedStaticData_; }
    slot_t *packedStaticData() const { return packedStaticData_; }
    size_t packedStaticDataSize() const { return packedStaticDataSize_; }
    void adoptPackedStaticData(slot_t *data, size_t size);
    void clearStaticDraftData();

    const std::string &nodeDebugEntityId(const Node *node) const;
    const std::string &nodePortName(const Node *node) const;
    const std::string &nodeAccsKey(const Node *node) const;
    const std::string *tryNodeAccsKey(const Node *node) const;
    void registerNodePortName(const Node *node, std::string name);
    void registerNodeAccsKey(const Node *node, std::string key);
    void setNodeDebugEntityId(const Node *node, std::string id);
    void eraseNodeColdData(const Node *node);

    ::OperatorIndex *registerOperIndex(std::shared_ptr<::OperatorIndex> idx);
    std::shared_ptr<::OperatorIndex> lookupOperIndex(const ::OperatorIndex *raw) const;

  private:
    static slot_t makeStaticSlot(const data_ptr_t &data, camel::core::mm::IAllocator &allocator);

    Graph &owner_;
    std::unique_ptr<DraftGraphBuilder> draftBuilder_;
    std::vector<Node *> ownedNodes_;
    static_slot_vec_t staticDataArr_ = {NullSlot};
    bool hasPackedStaticData_        = false;
    slot_t *packedStaticData_        = nullptr;
    size_t packedStaticDataSize_     = 0;
    size_t runtimeDataSize_          = 1;
    std::unordered_map<const Node *, std::string> nodeStableIds_;
    std::unordered_map<const Node *, std::string> nodePortNames_;
    std::unordered_map<const Node *, std::string> nodeAccsKeys_;
    std::unordered_map<const Node *, runtime::gc_node_ref_t> draftNodeIds_;
    std::unordered_map<const ::OperatorIndex *, std::shared_ptr<::OperatorIndex>>
        operIndexRegistry_;
};

} // namespace camel::compile::gir
