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
 * Updated: Oct. 19, 2026
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
    body.value    = camel::core::internSymbol(key);
    return bytesForStruct(body);
}

std::vector<std::byte> bytesForOperPayload(operator_t op, std::string_view uri) {
    GCOperBody body;
    body.op   = op;
    body.oper = camel::core::OperatorTable::instance().intern(uri);
    return bytesForStruct(body);
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
    gc_node_ref_t replacement, bool dedupInsert = false) {
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
        if (!dedupInsert || std::find(inputs.begin(), inputs.end(), nodeId) == inputs.end()) {
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

gc_slot_idx_t
ensureValueSlot(GraphDraft &draft, camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    if (dataIndex != 0) {
        return dataIndex;
    }
    if (type == nullptr) {
        return 0;
    }
    return draft.allocateRuntimeSlot(type);
}

} // namespace

std::span<std::byte> GraphDraft::mutablePayloadOf(gc_node_ref_t id) {
    requireOneWriting("mutablePayloadOf");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot get mutable payload view from a missing draft node.");
    touchRefs();
    return DraftNodeView::payload(draftNode);
}

std::span<std::byte> GraphDraft::mutablePayloadOf(DraftNode *draftNode) {
    requireOneWriting("mutablePayloadOf");
    ++mutations_;
    ASSERT(containsNode(draftNode), "Cannot get mutable payload view from a foreign draft node.");
    touchRefs();
    return DraftNodeView::payload(draftNode);
}

void GraphDraft::retargetOperNode(gc_node_ref_t id, std::string_view uri) {
    requireOneWriting("retargetOperNode");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(
        draftNode != nullptr && draftNode->header.kind == GCNodeKind::Oper,
        "Can only retarget an OPER node.");
    std::span<std::byte> payload = mutablePayloadOf(draftNode);
    ASSERT(payload.size() >= sizeof(GCOperBody), "OPER payload is too small.");
    auto *body = reinterpret_cast<GCOperBody *>(payload.data());
    // The operator is now named by its URI: whatever `op` the node resolved to belongs to the
    // operator it ran before.
    body->op   = nullptr;
    body->oper = camel::core::OperatorTable::instance().intern(uri);
}

gc_node_ref_t GraphDraft::addDataNode(
    camel::core::type::Type *type, gc_slot_idx_t dataIndex, uint8_t runtimeFlags) {
    ++mutations_;
    return addNode(
        DraftNodeInit{
            .dataIndex    = dataIndex,
            .dataType     = type,
            .kind         = GCNodeKind::Data,
            .runtimeFlags = runtimeFlags});
}

gc_node_ref_t GraphDraft::addPortNode(
    camel::core::type::Type *type, gc_slot_idx_t dataIndex, uint8_t runtimeFlags) {
    ++mutations_;
    return addNode(
        DraftNodeInit{
            .dataIndex    = dataIndex,
            .dataType     = type,
            .kind         = GCNodeKind::Port,
            .runtimeFlags = runtimeFlags});
}

gc_node_ref_t GraphDraft::addCastNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex = ensureValueSlot(*this, type, dataIndex);
    return addNode(
        DraftNodeInit{.dataIndex = dataIndex, .dataType = type, .kind = GCNodeKind::Cast});
}

gc_node_ref_t GraphDraft::addCopyNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex = ensureValueSlot(*this, type, dataIndex);
    return addNode(
        DraftNodeInit{.dataIndex = dataIndex, .dataType = type, .kind = GCNodeKind::Copy});
}

gc_node_ref_t GraphDraft::addFillNode(
    camel::core::type::Type *type, const GCFillBody &body, gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex          = ensureValueSlot(*this, type, dataIndex);
    const auto payload = bytesForStruct(body);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Fill,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addFillNode(
    camel::core::type::Type *type, std::span<const std::byte> payload, gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex = ensureValueSlot(*this, type, dataIndex);
    ASSERT(
        payload.empty() || payload.size_bytes() >= sizeof(GCFillBody),
        "FILL payload must contain at least GCFillBody.");
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Fill,
            .payload   = std::vector<std::byte>(payload.begin(), payload.end()),
        });
}

gc_node_ref_t GraphDraft::addAccsNode(
    camel::core::type::Type *type, uint32_t tupleIndex, gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex = ensureValueSlot(*this, type, dataIndex);
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
    ++mutations_;
    dataIndex          = ensureValueSlot(*this, type, dataIndex);
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
    ++mutations_;
    dataIndex          = ensureValueSlot(*this, type, dataIndex);
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
    ++mutations_;
    dataIndex = ensureValueSlot(*this, type, dataIndex);
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
    ++mutations_;
    dataIndex          = ensureValueSlot(*this, type, dataIndex);
    const auto payload = bytesForStruct(body);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Call,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addFuncNode(
    GraphId calleeGraph, camel::core::type::Type *type, uint8_t runtimeFlags,
    gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex = ensureValueSlot(*this, type, dataIndex);
    GCFuncRef body{.ref = payloadGraphIndexOf(calleeGraph)};
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

gc_node_ref_t GraphDraft::addLoopNode(
    const GCLoopBody &body, camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex = ensureValueSlot(*this, type, dataIndex);
    const GCLoopRef ref{
        .kind            = body.kind,
        .condRef         = payloadGraphIndexOf(body.condGraph),
        .bodyRef         = payloadGraphIndexOf(body.bodyGraph),
        .collectionCount = body.collectionCount,
    };
    const auto payload = bytesForStruct(ref);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type,
            .kind      = GCNodeKind::Loop,
            .payload   = payload,
        });
}

gc_node_ref_t GraphDraft::addOperNode(
    camel::core::type::Type *type, operator_t op, std::string_view uri, gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex          = ensureValueSlot(*this, type, dataIndex);
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
    ++mutations_;
    return addNode(DraftNodeInit{.dataIndex = 0, .dataType = nullptr, .kind = GCNodeKind::Sync});
}

gc_node_ref_t GraphDraft::addGateNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex = ensureValueSlot(*this, type, dataIndex);
    return addNode(
        DraftNodeInit{.dataIndex = dataIndex, .dataType = type, .kind = GCNodeKind::Gate});
}

gc_node_ref_t GraphDraft::addDrefNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex) {
    ++mutations_;
    dataIndex = ensureValueSlot(*this, type, dataIndex);
    return addNode(
        DraftNodeInit{
            .dataIndex = dataIndex,
            .dataType  = type ? type : camel::core::type::Type::Void(),
            .kind      = GCNodeKind::Dref,
        });
}

void GraphDraft::appendInput(DraftEdgeKind kind, gc_node_ref_t userId, gc_node_ref_t inputId) {
    requireOneWriting("appendInput");
    ++mutations_;
    ASSERT(node(userId) != nullptr, "Cannot append input to a missing draft node.");
    ASSERT(node(inputId) != nullptr, "Cannot append a missing input draft node.");
    // Value inputs are positional and may legitimately repeat, e.g. `(x, x)`, `[x, x]`, or
    // `x + x`; `with` inputs carry ordered payloads for FILL/JOIN/CALL. The runtime graph must
    // preserve that multiplicity instead of collapsing repeated edges into a set. A control
    // input is a dependency, which is stated once.
    if (kind == DraftEdgeKind::Ctrl) {
        const auto present = ctrlInputsOf(userId);
        const auto users   = ctrlUsersOf(inputId);
        const bool already = present.size() <= users.size()
                                 ? std::find(present.begin(), present.end(), inputId) !=
                                       present.end()
                                 : std::find(users.begin(), users.end(), userId) != users.end();
        if (already) {
            return;
        }
    }
    appendInputEntry(userId, kind, inputId);
    appendUserRef(
        inputId,
        userId,
        kind == DraftEdgeKind::Norm,
        kind == DraftEdgeKind::With,
        kind == DraftEdgeKind::Ctrl);
}

bool GraphDraft::unlinkInput(DraftEdgeKind kind, gc_node_ref_t userId, gc_node_ref_t inputId) {
    requireOneWriting("unlinkInput");
    ++mutations_;
    DraftNode *draftNode = node(userId);
    ASSERT(draftNode != nullptr, "Cannot unlink input from a missing draft node.");
    // The first entry that names `inputId` goes; the entries after it move down.
    auto group = mutableInputs(draftNode, kind);
    auto it    = std::find(group.begin(), group.end(), inputId);
    if (it == group.end()) {
        return false;
    }
    DraftNodeHeader &h = draftNode->header;
    auto *base         = reinterpret_cast<gc_node_ref_t *>(draftNode->tail + h.payloadBytes);
    const size_t total = static_cast<size_t>(h.normInputCount) + h.withInputCount + h.ctrlInputCount;
    const size_t at    = static_cast<size_t>(&*it - base);
    std::memmove(base + at, base + at + 1, (total - at - 1) * sizeof(gc_node_ref_t));
    (kind == DraftEdgeKind::Norm   ? h.normInputCount
     : kind == DraftEdgeKind::With ? h.withInputCount
                                   : h.ctrlInputCount)--;
    // The user stays a user of `inputId` while another entry of the group names it.
    group = mutableInputs(draftNode, kind);
    if (std::find(group.begin(), group.end(), inputId) == group.end()) {
        removeUserRef(
            inputId,
            userId,
            kind == DraftEdgeKind::Norm,
            kind == DraftEdgeKind::With,
            kind == DraftEdgeKind::Ctrl);
    }
    return true;
}

void GraphDraft::replaceInput(
    DraftEdgeKind kind, gc_node_ref_t userId, gc_node_ref_t oldInputId, gc_node_ref_t newInputId) {
    requireOneWriting("replaceInput");
    ++mutations_;
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
    requireOneWriting("setNodeDataType");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set data type on a missing draft node.");
    draftNode->header.dataType = type;
}

void GraphDraft::setNodeDataIndex(gc_node_ref_t id, gc_slot_idx_t dataIndex) {
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set data index on a missing draft node.");
    draftNode->header.dataIndex = dataIndex;
}

void GraphDraft::setNodeRuntimeFlags(gc_node_ref_t id, uint8_t runtimeFlags) {
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set runtime flags on a missing draft node.");
    draftNode->header.runtimeFlags = runtimeFlags;
}

void GraphDraft::setLoopCollectionCount(gc_node_ref_t id, uint32_t count) {
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(
        draftNode != nullptr && draftNode->header.kind == GCNodeKind::Loop,
        "Only a LOOP node has collection slots.");
    auto *body            = DraftNodeView::payloadAs<GCLoopRef>(draftNode);
    body->collectionCount = count;
}

void GraphDraft::rewriteNode(gc_node_ref_t id, const DraftNodeInit &init) {
    requireOneWriting("rewriteNode");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot rewrite a missing draft node.");
    touchRefs();

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

    // A rewritten node keeps its source origin, its numeric permissions and its attributes
    // unless the rewrite gives it others.
    const uint64_t origin     = draftNode->header.origin;
    const uint8_t permissions = draftNode->header.permissions;
    const uint32_t attrs      = draftNode->header.attrs;
    const GCNodeKind oldKind  = draftNode->header.kind;
    replaceNodeStorage(id, rebuildNode(id, init));
    if (init.kind != oldKind) {
        noteNodeKind(id, init.kind);
    }
    if (init.origin == 0) {
        node(id)->header.origin = origin;
    }
    if (init.permissions == 0) {
        node(id)->header.permissions = permissions;
    }
    if (init.attrs == 0) {
        node(id)->header.attrs = attrs;
    }
    if (init.region != 0) {
        node(id)->header.region = init.region;
    }

    // The input lists are read back from the rebuilt node, not from `init`: a caller that
    // snapshots the node and changes some lists (snapshotNodeInit) passes spans that view the
    // node's own tail block, which rebuildNode has just laid out anew (in place when it fits).
    // With one norm input fewer, the span that used to cover the control inputs covers other
    // entries, and the control inputs were left without their user reference: a call
    // specialized and then inlined in one session (std::opt) lost the control edge from the
    // statement before it, and that statement's effects never ran.
    const auto copy = [](std::span<const gc_node_ref_t> refs) {
        return std::vector<gc_node_ref_t>(refs.begin(), refs.end());
    };
    const DraftNode *rebuilt = node(id);
    const auto newNormInputs = copy(DraftNodeView::normInputs(rebuilt));
    const auto newWithInputs = copy(DraftNodeView::withInputs(rebuilt));
    const auto newCtrlInputs = copy(DraftNodeView::ctrlInputs(rebuilt));
    for (gc_node_ref_t input : newNormInputs) {
        appendUserRef(input, id, true, false, false);
    }
    for (gc_node_ref_t input : newWithInputs) {
        appendUserRef(input, id, false, true, false);
    }
    for (gc_node_ref_t input : newCtrlInputs) {
        appendUserRef(input, id, false, false, true);
    }
}

void GraphDraft::appendNormPort(gc_node_ref_t id) {
    requireOneWriting("appendNormPort");
    ++mutations_;
    ASSERT(node(id) != nullptr, "Cannot register a missing draft node as a norm port.");
    appendUniqueNodeRef(normPorts_, id);
}

void GraphDraft::appendWithPort(gc_node_ref_t id) {
    requireOneWriting("appendWithPort");
    ++mutations_;
    ASSERT(node(id) != nullptr, "Cannot register a missing draft node as a with port.");
    appendUniqueNodeRef(withPorts_, id);
}

void GraphDraft::appendClosureNode(gc_node_ref_t id) {
    requireOneWriting("appendClosureNode");
    ++mutations_;
    ASSERT(node(id) != nullptr, "Cannot register a missing draft node as a closure node.");
    appendUniqueNodeRef(closureNodes_, id);
}

void GraphDraft::removeNormPort(gc_node_ref_t id) {
    requireOneWriting("removeNormPort");
    ++mutations_;
    std::erase(normPorts_, id);
}

void GraphDraft::removeWithPort(gc_node_ref_t id) {
    requireOneWriting("removeWithPort");
    ++mutations_;
    std::erase(withPorts_, id);
}

void GraphDraft::removeClosureNode(gc_node_ref_t id) {
    requireOneWriting("removeClosureNode");
    ++mutations_;
    std::erase(closureNodes_, id);
}

void GraphDraft::appendUniqueNodeRef(std::vector<gc_node_ref_t> &refs, gc_node_ref_t id) {
    ++mutations_;
    if (std::find(refs.begin(), refs.end(), id) == refs.end()) {
        refs.push_back(id);
    }
}

} // namespace camel::runtime
