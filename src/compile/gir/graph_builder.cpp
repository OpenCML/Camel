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

#include "graph_builder.h"

#include "camel/compile/gir/graph.h"
#include "camel/compile/gir/nodes.h"
#include "camel/core/rtdata/conv.h"

#include <format>
#include <limits>
#include <stdexcept>

namespace camel::compile::gir {

using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;

GraphBuilder::GraphBuilder(Graph &owner)
    : owner_(owner), draftBuilder_(std::make_unique<DraftGraphBuilder>()) {}

Node *GraphBuilder::ownNode(Node *node) {
    ASSERT(node != nullptr, "Cannot own null node.");
    ownedNodes_.push_back(node);
    return node;
}

void GraphBuilder::bindDraftNode(Node *node, runtime::gc_node_ref_t draftId) {
    ASSERT(node != nullptr, "Cannot bind null compile node to draft node.");
    draftNodeIds_[node] = draftId;
}

runtime::gc_node_ref_t GraphBuilder::draftNodeId(const Node *node) const {
    auto it = draftNodeIds_.find(node);
    ASSERT(it != draftNodeIds_.end(), "Compile node is not bound to a draft node.");
    return it->second;
}

runtime::gc_node_ref_t GraphBuilder::tryDraftNodeId(const Node *node) const {
    auto it = draftNodeIds_.find(node);
    return it != draftNodeIds_.end() ? it->second : runtime::kInvalidNodeRef;
}

bool GraphBuilder::hasDraftNode(const Node *node) const {
    return node != nullptr && draftNodeIds_.contains(node);
}

void GraphBuilder::eraseDraftNode(const Node *node) {
    if (!node) {
        return;
    }
    draftNodeIds_.erase(node);
}

slot_t
GraphBuilder::makeStaticSlot(const data_ptr_t &data, camel::core::mm::IAllocator &allocator) {
    ASSERT(data != nullptr, "Static data cannot be null.");
    if (data->type()->isGCTraced()) {
        Object *obj = makeGCRefFromGCTracedData(data, allocator);
        return toSlot<Object *>(obj);
    }
    if (data->type()->isPrimitive()) {
        return makeSlotFromPrimitiveData(data);
    }
    ASSERT(
        false,
        std::format(
            "Unsupported static data type '{}' for slot conversion.",
            data->type()->toString()));
    return NullSlot;
}

data_idx_t GraphBuilder::addStaticSlot(slot_t slot) {
    staticDataArr_.push_back(slot);
    if (staticDataArr_.size() > static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("staticDataArr_ exceeds arr_size_t max value");
    }
    data_idx_t index      = -static_cast<data_idx_t>(staticDataArr_.size() - 1);
    hasPackedStaticData_  = false;
    packedStaticData_     = nullptr;
    packedStaticDataSize_ = 0;
    return index;
}

data_idx_t GraphBuilder::addStaticData(const data_ptr_t &data) {
    data_idx_t index      = addStaticSlot(makeStaticSlot(data, arena()->allocator()));
    hasPackedStaticData_  = false;
    packedStaticData_     = nullptr;
    packedStaticDataSize_ = 0;
    return index;
}

data_idx_t GraphBuilder::addRuntimeData() {
    if (runtimeDataSize_ > static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("runtimeDataSize_ exceeds arr_size_t max value");
    }
    data_idx_t index      = static_cast<data_idx_t>(runtimeDataSize_++);
    hasPackedStaticData_  = false;
    packedStaticData_     = nullptr;
    packedStaticDataSize_ = 0;
    return index;
}

void GraphBuilder::setStaticSlot(data_idx_t index, slot_t slot) {
    ASSERT(index < 0, "Static data index must be negative.");
    const size_t idx = static_cast<size_t>(-index);
    ASSERT(
        idx < staticDataArr_.size(),
        std::format("Static data index {} is out of range.", index));
    staticDataArr_[idx]   = slot;
    hasPackedStaticData_  = false;
    packedStaticData_     = nullptr;
    packedStaticDataSize_ = 0;
}

void GraphBuilder::setStaticData(data_idx_t index, const data_ptr_t &data) {
    setStaticSlot(index, makeStaticSlot(data, arena()->allocator()));
}

slot_t GraphBuilder::getStaticDataSlot(data_idx_t index) const {
    if (index >= 0) {
        throw std::runtime_error(std::format("Static data index must be negative, got {}.", index));
    }
    const size_t idx  = static_cast<size_t>(-index);
    const size_t size = staticDataSize();
    if (idx >= size) {
        throw std::runtime_error(
            std::format(
                "Static data index {} is out of range (size={}, packed={}, packedSize={}).",
                index,
                size,
                hasPackedStaticData_,
                packedStaticDataSize_));
    }
    if (hasPackedStaticData_) {
        if (packedStaticData_ == nullptr) {
            throw std::runtime_error(
                std::format(
                    "Packed static data is null while packed flag is set (index={}, size={}).",
                    index,
                    packedStaticDataSize_));
        }
        return packedStaticData_[idx];
    }
    return staticDataArr_[idx];
}

size_t GraphBuilder::staticDataSize() const {
    return hasPackedStaticData_ ? packedStaticDataSize_ : staticDataArr_.size();
}

void GraphBuilder::adoptPackedStaticData(slot_t *data, size_t size) {
    packedStaticData_     = data;
    packedStaticDataSize_ = size;
    hasPackedStaticData_  = true;
}

void GraphBuilder::clearStaticDraftData() {
    staticDataArr_.clear();
    staticDataArr_.shrink_to_fit();
}

const std::string &GraphBuilder::nodeDebugEntityId(const Node *node) const {
    auto it = nodeStableIds_.find(node);
    ASSERT(it != nodeStableIds_.end(), "Node stableId not found in GraphBuilder.");
    return it->second;
}

const std::string &GraphBuilder::nodePortName(const Node *node) const {
    auto it = nodePortNames_.find(node);
    ASSERT(it != nodePortNames_.end(), "Port name not found in GraphBuilder.");
    return it->second;
}

const std::string &GraphBuilder::nodeAccsKey(const Node *node) const {
    auto it = nodeAccsKeys_.find(node);
    ASSERT(it != nodeAccsKeys_.end(), "ACCS key not found in GraphBuilder.");
    return it->second;
}

const std::string *GraphBuilder::tryNodeAccsKey(const Node *node) const {
    auto it = nodeAccsKeys_.find(node);
    return it != nodeAccsKeys_.end() ? &it->second : nullptr;
}

void GraphBuilder::registerNodePortName(const Node *node, std::string name) {
    ASSERT(node != nullptr, "Cannot register a port name for a null node.");
    nodePortNames_[node] = std::move(name);
}

void GraphBuilder::registerNodeAccsKey(const Node *node, std::string key) {
    ASSERT(node != nullptr, "Cannot register an ACCS key for a null node.");
    nodeAccsKeys_[node] = std::move(key);
}

void GraphBuilder::setNodeDebugEntityId(const Node *node, std::string id) {
    ASSERT(node != nullptr, "Cannot register a node debug id for a null node.");
    nodeStableIds_[node] = std::move(id);
}

void GraphBuilder::eraseNodeColdData(const Node *node) {
    if (!node) {
        return;
    }
    nodeStableIds_.erase(node);
    nodePortNames_.erase(node);
    nodeAccsKeys_.erase(node);
    eraseDraftNode(node);
}

::OperatorIndex *GraphBuilder::registerOperIndex(std::shared_ptr<::OperatorIndex> idx) {
    auto *raw               = idx.get();
    operIndexRegistry_[raw] = std::move(idx);
    return raw;
}

std::shared_ptr<::OperatorIndex> GraphBuilder::lookupOperIndex(const ::OperatorIndex *raw) const {
    auto it = operIndexRegistry_.find(raw);
    ASSERT(it != operIndexRegistry_.end(), "OperatorIndex not registered in GraphBuilder.");
    return it->second;
}

} // namespace camel::compile::gir
