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
 * GraphDraft construction-oriented editing API.
 *
 * `draft.cpp` implements decode/encode and low-level mutation primitives.
 * This file layers graph-construction helpers on top of that core so both
 * compile-time building and runtime rewriting can speak the same editable
 * graph vocabulary without reviving the legacy compile-time `Node` object
 * hierarchy.
 */

#include "camel/runtime/draft.h"

#include "camel/core/type/base.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace camel::runtime {

namespace {

std::vector<std::byte> bytesForStruct(const auto &body) {
    std::vector<std::byte> bytes(sizeof(body));
    std::memcpy(bytes.data(), &body, sizeof(body));
    return bytes;
}

std::vector<std::byte> bytesForAccsStructKey(std::string_view key) {
    GCAccsBody body;
    body.accsKind = GCAccsKind::StructKey;
    body.keyBytes = static_cast<uint16_t>(key.size());

    std::vector<std::byte> bytes(sizeof(GCAccsBody) + key.size());
    std::memcpy(bytes.data(), &body, sizeof(body));
    if (!key.empty()) {
        std::memcpy(bytes.data() + sizeof(GCAccsBody), key.data(), key.size());
    }
    return bytes;
}

std::vector<std::byte> bytesForOperPayload(operator_t op, std::string_view uri) {
    GCOperBody body;
    body.op       = op;
    body.uriBytes = static_cast<uint16_t>(uri.size());

    std::vector<std::byte> bytes(sizeof(GCOperBody) + uri.size());
    std::memcpy(bytes.data(), &body, sizeof(body));
    if (!uri.empty()) {
        std::memcpy(bytes.data() + sizeof(GCOperBody), uri.data(), uri.size());
    }
    return bytes;
}

std::vector<std::byte> bytesForBranchPayload(
    gc_node_ref_t joinRef, std::span<const GCBranchArm> arms, gc_node_ref_t defaultArm) {
    DraftBrchPayload body;
    body.join       = joinRef;
    body.armCount   = static_cast<gc_cnt_t>(arms.size());
    body.defaultArm = defaultArm;

    std::vector<std::byte> bytes(sizeof(DraftBrchPayload) + arms.size_bytes());
    std::memcpy(bytes.data(), &body, sizeof(body));
    if (!arms.empty()) {
        std::memcpy(bytes.data() + sizeof(DraftBrchPayload), arms.data(), arms.size_bytes());
    }
    return bytes;
}

std::vector<gc_node_ref_t> mutatedInputs(
    std::span<const gc_node_ref_t> current, gc_node_ref_t nodeId, bool insert, bool replace,
    gc_node_ref_t replacement) {
    std::vector<gc_node_ref_t> inputs(current.begin(), current.end());
    if (replace) {
        for (gc_node_ref_t &value : inputs) {
            if (value == nodeId) {
                value = replacement;
            }
        }
        return inputs;
    }
    if (insert) {
        if (std::find(inputs.begin(), inputs.end(), nodeId) == inputs.end()) {
            inputs.push_back(nodeId);
        }
        return inputs;
    }
    auto it = std::find(inputs.begin(), inputs.end(), nodeId);
    if (it != inputs.end()) {
        inputs.erase(it);
    }
    return inputs;
}

} // namespace

std::span<std::byte> GraphDraft::mutablePayloadOf(gc_node_ref_t id) {
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot get mutable payload view from a missing draft node.");
    return DraftNodeView::payload(draftNode);
}

gc_node_ref_t GraphDraft::addDataNode(
    camel::core::type::Type *type, gc_slot_idx_t dataIndex, uint8_t runtimeFlags) {
    return addNode(
        DraftNodeInit{
            .dataIndex    = dataIndex,
            .dataType     = type,
            .kind         = GCNodeKind::Data,
            .runtimeFlags = runtimeFlags});
}

gc_node_ref_t GraphDraft::addPortNode(
    camel::core::type::Type *type, gc_slot_idx_t dataIndex, uint8_t runtimeFlags) {
    return addNode(
        DraftNodeInit{
            .dataIndex    = dataIndex,
            .dataType     = type,
            .kind         = GCNodeKind::Port,
            .runtimeFlags = runtimeFlags});
}

gc_node_ref_t GraphDraft::addCastNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    return addNode(
        DraftNodeInit{.dataIndex = dataIndex, .dataType = type, .kind = GCNodeKind::Cast});
}

gc_node_ref_t GraphDraft::addCopyNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    return addNode(
        DraftNodeInit{.dataIndex = dataIndex, .dataType = type, .kind = GCNodeKind::Copy});
}

gc_node_ref_t GraphDraft::addFillNode(
    camel::core::type::Type *type, const GCFillBody &body, gc_slot_idx_t dataIndex) {
    const auto payload = bytesForStruct(body);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Fill,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addAccsNode(
    camel::core::type::Type *type, uint32_t tupleIndex, gc_slot_idx_t dataIndex) {
    GCAccsBody body;
    body.accsKind      = GCAccsKind::TupleIndex;
    body.value         = tupleIndex;
    const auto payload = bytesForStruct(body);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Accs,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addAccsNode(
    camel::core::type::Type *type, std::string_view structKey, gc_slot_idx_t dataIndex) {
    const auto payload = bytesForAccsStructKey(structKey);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Accs,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addBrchNode(
    camel::core::type::Type *type, gc_node_ref_t joinRef, std::span<const GCBranchArm> arms,
    gc_node_ref_t defaultArm, gc_slot_idx_t dataIndex) {
    const auto payload = bytesForBranchPayload(joinRef, arms, defaultArm);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Brch,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addJoinNode(
    camel::core::type::Type *type, gc_node_ref_t brchRef, gc_cnt_t armCount,
    gc_slot_idx_t dataIndex) {
    GCJoinBody body;
    body.brch          = brchRef;
    body.armCount      = armCount;
    const auto payload = bytesForStruct(body);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Join,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addCallNode(
    camel::core::type::Type *type, const GCCallBody &body, gc_slot_idx_t dataIndex) {
    const auto payload = bytesForStruct(body);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Call,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addBindNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    return addNode(
        DraftNodeInit{.dataIndex = dataIndex, .dataType = type, .kind = GCNodeKind::Bind});
}

gc_node_ref_t GraphDraft::addFuncNode(
    GCGraph *calleeGraph, camel::core::type::Type *type, uint8_t runtimeFlags,
    gc_slot_idx_t dataIndex) {
    GCFuncBody body{.calleeGraph = calleeGraph};
    const auto payload = bytesForStruct(body);
    return addNode(
        DraftNodeInit{
            .dataIndex    = dataIndex,
            .dataType     = type,
            .kind         = GCNodeKind::Func,
            .runtimeFlags = runtimeFlags,
            .payload      = payload,
        });
}

gc_node_ref_t GraphDraft::addOperNode(
    camel::core::type::Type *type, operator_t op, std::string_view uri, gc_slot_idx_t dataIndex) {
    const auto payload = bytesForOperPayload(op, uri);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Oper,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addSyncNode() {
    return addNode(DraftNodeInit{.dataIndex = 0, .dataType = nullptr, .kind = GCNodeKind::Sync});
}

gc_node_ref_t GraphDraft::addGateNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    return addNode(
        DraftNodeInit{.dataIndex = dataIndex, .dataType = type, .kind = GCNodeKind::Gate});
}

gc_node_ref_t GraphDraft::addDrefNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type ? type : camel::core::type::Type::Void(),
            .kind      = GCNodeKind::Dref,
        });
}

void GraphDraft::appendInput(DraftEdgeKind kind, gc_node_ref_t userId, gc_node_ref_t inputId) {
    ASSERT(node(userId) != nullptr, "Cannot append input to a missing draft node.");
    ASSERT(node(inputId) != nullptr, "Cannot append a missing input draft node.");
    switch (kind) {
    case DraftEdgeKind::Norm: {
        const auto inputs =
            mutatedInputs(normInputsOf(userId), inputId, true, false, kInvalidNodeRef);
        setNormInputs(userId, inputs);
        break;
    }
    case DraftEdgeKind::With: {
        const auto inputs =
            mutatedInputs(withInputsOf(userId), inputId, true, false, kInvalidNodeRef);
        setWithInputs(userId, inputs);
        break;
    }
    case DraftEdgeKind::Ctrl: {
        const auto inputs =
            mutatedInputs(ctrlInputsOf(userId), inputId, true, false, kInvalidNodeRef);
        setCtrlInputs(userId, inputs);
        break;
    }
    }
}

bool GraphDraft::unlinkInput(DraftEdgeKind kind, gc_node_ref_t userId, gc_node_ref_t inputId) {
    ASSERT(node(userId) != nullptr, "Cannot unlink input from a missing draft node.");
    const auto beforeNorm = normInputsOf(userId).size();
    const auto beforeWith = withInputsOf(userId).size();
    const auto beforeCtrl = ctrlInputsOf(userId).size();
    switch (kind) {
    case DraftEdgeKind::Norm: {
        const auto inputs =
            mutatedInputs(normInputsOf(userId), inputId, false, false, kInvalidNodeRef);
        setNormInputs(userId, inputs);
        return inputs.size() != beforeNorm;
    }
    case DraftEdgeKind::With: {
        const auto inputs =
            mutatedInputs(withInputsOf(userId), inputId, false, false, kInvalidNodeRef);
        setWithInputs(userId, inputs);
        return inputs.size() != beforeWith;
    }
    case DraftEdgeKind::Ctrl: {
        const auto inputs =
            mutatedInputs(ctrlInputsOf(userId), inputId, false, false, kInvalidNodeRef);
        setCtrlInputs(userId, inputs);
        return inputs.size() != beforeCtrl;
    }
    }
    return false;
}

void GraphDraft::replaceInput(
    DraftEdgeKind kind, gc_node_ref_t userId, gc_node_ref_t oldInputId, gc_node_ref_t newInputId) {
    ASSERT(node(userId) != nullptr, "Cannot replace input on a missing draft node.");
    ASSERT(node(newInputId) != nullptr, "Cannot replace with a missing draft node.");
    switch (kind) {
    case DraftEdgeKind::Norm: {
        const auto inputs =
            mutatedInputs(normInputsOf(userId), oldInputId, false, true, newInputId);
        setNormInputs(userId, inputs);
        break;
    }
    case DraftEdgeKind::With: {
        const auto inputs =
            mutatedInputs(withInputsOf(userId), oldInputId, false, true, newInputId);
        setWithInputs(userId, inputs);
        break;
    }
    case DraftEdgeKind::Ctrl: {
        const auto inputs =
            mutatedInputs(ctrlInputsOf(userId), oldInputId, false, true, newInputId);
        setCtrlInputs(userId, inputs);
        break;
    }
    }
}

void GraphDraft::setNodeDataType(gc_node_ref_t id, camel::core::type::Type *type) {
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set data type on a missing draft node.");
    draftNode->header.dataType = type;
}

void GraphDraft::setNodeDataIndex(gc_node_ref_t id, gc_slot_idx_t dataIndex) {
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set data index on a missing draft node.");
    draftNode->header.dataIndex = dataIndex;
}

void GraphDraft::setNodeRuntimeFlags(gc_node_ref_t id, uint8_t runtimeFlags) {
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set runtime flags on a missing draft node.");
    draftNode->header.runtimeFlags = runtimeFlags;
}

void GraphDraft::rewriteNode(gc_node_ref_t id, const DraftNodeInit &init) {
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot rewrite a missing draft node.");

    const std::vector<gc_node_ref_t> oldNormInputs(
        DraftNodeView::normInputs(draftNode).begin(),
        DraftNodeView::normInputs(draftNode).end());
    const std::vector<gc_node_ref_t> oldWithInputs(
        DraftNodeView::withInputs(draftNode).begin(),
        DraftNodeView::withInputs(draftNode).end());
    const std::vector<gc_node_ref_t> oldCtrlInputs(
        DraftNodeView::ctrlInputs(draftNode).begin(),
        DraftNodeView::ctrlInputs(draftNode).end());

    for (gc_node_ref_t oldInput : oldNormInputs) {
        removeUserRef(oldInput, id, true, false, false);
    }
    for (gc_node_ref_t oldInput : oldWithInputs) {
        removeUserRef(oldInput, id, false, true, false);
    }
    for (gc_node_ref_t oldInput : oldCtrlInputs) {
        removeUserRef(oldInput, id, false, false, true);
    }

    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : init.normInputs) {
        appendUserRef(input, id, true, false, false);
    }
    for (gc_node_ref_t input : init.withInputs) {
        appendUserRef(input, id, false, true, false);
    }
    for (gc_node_ref_t input : init.ctrlInputs) {
        appendUserRef(input, id, false, false, true);
    }
}

void GraphDraft::appendNormPort(gc_node_ref_t id) {
    ASSERT(node(id) != nullptr, "Cannot register a missing draft node as a norm port.");
    appendUniqueNodeRef(normPorts_, id);
}

void GraphDraft::appendWithPort(gc_node_ref_t id) {
    ASSERT(node(id) != nullptr, "Cannot register a missing draft node as a with port.");
    appendUniqueNodeRef(withPorts_, id);
}

void GraphDraft::appendClosureNode(gc_node_ref_t id) {
    ASSERT(node(id) != nullptr, "Cannot register a missing draft node as a closure node.");
    appendUniqueNodeRef(closureNodes_, id);
}

void GraphDraft::removeNormPort(gc_node_ref_t id) { std::erase(normPorts_, id); }

void GraphDraft::removeWithPort(gc_node_ref_t id) { std::erase(withPorts_, id); }

void GraphDraft::removeClosureNode(gc_node_ref_t id) { std::erase(closureNodes_, id); }

void GraphDraft::appendUniqueNodeRef(std::vector<gc_node_ref_t> &refs, gc_node_ref_t id) {
    if (std::find(refs.begin(), refs.end(), id) == refs.end()) {
        refs.push_back(id);
    }
}

} // namespace camel::runtime
