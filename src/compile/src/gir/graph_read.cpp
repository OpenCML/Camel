// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/compile/gir/graph_read.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <iostream>
#include <unordered_set>

namespace camel::compile::gir {
namespace {

/// Reads a payload body off a draft's tail. A draft keeps a node's payload as bytes at an
/// arbitrary offset, so the body is copied out instead of reinterpreted in place: the copy is what
/// makes this safe for a body with pointers in it (`GCOperBody`, `GCFuncRef`).
template <typename Body, typename Graph>
bool draftBodyOf(const Graph &draft, gc_node_ref_t ref, Body &out) {
    const std::span<const std::byte> payload = draft.payloadOf(ref);
    if (payload.size() < sizeof(Body)) {
        return false;
    }
    std::memcpy(&out, payload.data(), sizeof(Body));
    return true;
}

} // namespace

bool DraftGraphRead::alive(gc_node_ref_t ref) const {
    return kept_.empty() ? txn_.graph().alive(ref) : ref < kept_.size();
}

gc_node_ref_t DraftGraphRead::mappedIdOf(gc_node_ref_t id) const {
    if (kept_.empty()) {
        return id;
    }
    for (size_t i = 0; i < kept_.size(); ++i) {
        if (kept_[i] == id) {
            return static_cast<gc_node_ref_t>(i);
        }
    }
    return camel::runtime::kInvalidNodeRef;
}

std::vector<gc_node_ref_t>
DraftGraphRead::mappedListOf(std::span<const gc_node_ref_t> inputs) const {
    std::vector<gc_node_ref_t> mapped;
    mapped.reserve(inputs.size());
    for (gc_node_ref_t input : inputs) {
        mapped.push_back(mappedIdOf(input));
    }
    return mapped;
}

std::vector<gc_node_ref_t> DraftGraphRead::normInputsOf(gc_node_ref_t ref) const {
    return mappedListOf(txn_.graph().normInputsOf(sourceIdOf(ref)));
}

std::vector<gc_node_ref_t> DraftGraphRead::withInputsOf(gc_node_ref_t ref) const {
    return mappedListOf(txn_.graph().withInputsOf(sourceIdOf(ref)));
}

std::vector<gc_node_ref_t> DraftGraphRead::ctrlInputsOf(gc_node_ref_t ref) const {
    return mappedListOf(txn_.graph().ctrlInputsOf(sourceIdOf(ref)));
}

std::vector<gc_node_ref_t> DraftGraphRead::normPortsOf() const {
    return mappedListOf(txn_.graph().normPorts());
}

std::vector<gc_node_ref_t> DraftGraphRead::withPortsOf() const {
    return mappedListOf(txn_.graph().withPorts());
}

std::vector<gc_node_ref_t> DraftGraphRead::closureNodesOf() const {
    return mappedListOf(txn_.graph().closureNodes());
}

const camel::core::type::TupleType *DraftGraphRead::closureTypeOf() const {
    return txn_.graph().closureType();
}

uint32_t DraftGraphRead::regionOfNode(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = txn_.graph().header(sourceIdOf(ref));
    return header == nullptr ? 0 : header->region;
}

std::vector<gc_node_ref_t> DraftGraphRead::regionPortsOf(uint32_t id) const {
    return mappedListOf(txn_.graph().regionPortsOf(id));
}

std::vector<gc_node_ref_t> DraftGraphRead::syncInputsOf(gc_node_ref_t ref) const {
    return mappedListOf(txn_.graph().syncInputsOf(sourceIdOf(ref)));
}

gc_node_ref_t DraftGraphRead::exitRefOf() const {
    const gc_node_ref_t exit = txn_.graph().exitNode();
    return exit == kInvalidNodeRef ? kInvalidNodeRef : mappedIdOf(exit);
}

NodeFace DraftGraphRead::nodeFaceOf(gc_node_ref_t ref) const {
    NodeFace face;
    const camel::runtime::DraftNodeHeader *header = txn_.graph().header(sourceIdOf(ref));
    if (header == nullptr || header->kind == GCNodeKind::Dead) {
        return face;
    }
    face.alive     = true;
    face.kind      = header->kind;
    face.dataIndex = header->dataIndex;
    face.type      = header->dataType;
    face.flags     = header->runtimeFlags;
    face.payload   = header->payloadBytes != 0;
    return face;
}
GCNodeKind DraftGraphRead::kindOf(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = txn_.graph().header(sourceIdOf(ref));
    return header == nullptr ? GCNodeKind::Dead : header->kind;
}

gc_slot_idx_t DraftGraphRead::dataIndexOf(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = txn_.graph().header(sourceIdOf(ref));
    return header == nullptr ? 0 : header->dataIndex;
}

const camel::core::type::Type *DraftGraphRead::typeOf(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = txn_.graph().header(sourceIdOf(ref));
    return header == nullptr ? nullptr : header->dataType;
}

uint8_t DraftGraphRead::flagsOf(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = txn_.graph().header(sourceIdOf(ref));
    return header == nullptr ? 0 : header->runtimeFlags;
}

bool DraftGraphRead::hasPayload(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = txn_.graph().header(sourceIdOf(ref));
    return header != nullptr && header->payloadBytes != 0;
}

GraphId DraftGraphRead::calleeIdOf(gc_node_ref_t ref) const {
    if (kindOf(ref) != GCNodeKind::Func) {
        return camel::runtime::kInvalidGraphId;
    }
    const std::span<const std::byte> payload = txn_.graph().payloadOf(sourceIdOf(ref));
    if (payload.size() < sizeof(camel::runtime::GCFuncRef)) {
        return camel::runtime::kInvalidGraphId;
    }
    camel::runtime::GCFuncRef body{};
    std::memcpy(&body, payload.data(), sizeof(body));
    const std::span<const GraphId> callees = txn_.graph().payloadGraphs();
    return body.ref < callees.size() ? callees[body.ref] : camel::runtime::kInvalidGraphId;
}

std::span<const std::byte> DraftGraphRead::payloadOf(gc_node_ref_t ref) const {
    return txn_.graph().payloadOf(sourceIdOf(ref));
}
const NodeFace *GraphRead::node(gc_node_ref_t ref) const {
    // Asked for on every call rather than kept: an erased node is erased for the next ask, and a
    // face that answered from a table built once would keep answering for a node its graph no
    // longer has. The record itself is still the face's -- one slot per id, written again on each
    // ask -- so the pointer is valid for as long as the face is, and no longer.
    if (ref >= nodeSlotCount() || !alive(ref)) {
        return nullptr;
    }
    if (records_.size() < static_cast<size_t>(nodeSlotCount())) {
        records_.resize(static_cast<size_t>(nodeSlotCount()));
    }
    records_[ref] = nodeFaceOf(ref);
    return &records_[ref];
}

std::string GraphRead::operUriOf(gc_node_ref_t ref) const {
    if (kindOf(ref) != GCNodeKind::Oper) {
        return {};
    }
    camel::runtime::GCOperBody body{};
    if (!bodyOf(ref, body)) {
        return {};
    }
    return std::string(body.uri());
}

gc_cnt_t GraphRead::armCountOf(gc_node_ref_t ref) const {
    if (kindOf(ref) != GCNodeKind::Brch) {
        return 0;
    }
    camel::runtime::GCBrchBody body{};
    if (!bodyOf(ref, body)) {
        return 0;
    }
    return body.armCount;
}
std::vector<gc_node_ref_t> FrozenGraphRead::normPortsOf() const {
    const std::span<const gc_node_ref_t> ports = graph_.normPorts();
    return std::vector<gc_node_ref_t>(ports.begin(), ports.end());
}

std::vector<gc_node_ref_t> FrozenGraphRead::withPortsOf() const {
    const std::span<const gc_node_ref_t> ports = graph_.withPorts();
    return std::vector<gc_node_ref_t>(ports.begin(), ports.end());
}

std::vector<gc_node_ref_t> FrozenGraphRead::closureNodesOf() const {
    const std::span<const gc_node_ref_t> ports = graph_.closureNodes();
    return std::vector<gc_node_ref_t>(ports.begin(), ports.end());
}

const camel::core::type::TupleType *FrozenGraphRead::closureTypeOf() const {
    return graph_.closureType();
}

std::vector<gc_node_ref_t> FrozenGraphRead::regionPortsOf(uint32_t id) const {
    const std::span<const gc_node_ref_t> ports = graph_.regionPorts(id);
    return std::vector<gc_node_ref_t>(ports.begin(), ports.end());
}

std::vector<gc_node_ref_t> FrozenGraphRead::syncInputsOf(gc_node_ref_t ref) const {
    std::vector<gc_node_ref_t> preds;
    for (const camel::runtime::GCGraphSyncEdge &edge : graph_.syncPredsOf(ref)) {
        preds.push_back(edge.pred);
    }
    return preds;
}

NodeFace FrozenGraphRead::nodeFaceOf(gc_node_ref_t ref) const {
    NodeFace face;
    const camel::runtime::GCNode *node = graph_.node(ref);
    if (node == nullptr) {
        return face;
    }
    face.alive     = true;
    face.kind      = node->kind;
    face.dataIndex = node->dataIndex;
    face.type      = node->dataType();
    face.flags     = node->flags;
    face.payload   = node->hasPayload();
    return face;
}
bool FrozenGraphRead::alive(gc_node_ref_t ref) const {
    const camel::runtime::GCNode *node = graph_.node(ref);
    return node != nullptr && node->kind != GCNodeKind::Dead;
}

GCNodeKind FrozenGraphRead::kindOf(gc_node_ref_t ref) const {
    const camel::runtime::GCNode *node = graph_.node(ref);
    return node == nullptr ? GCNodeKind::Dead : node->kind;
}

gc_slot_idx_t FrozenGraphRead::dataIndexOf(gc_node_ref_t ref) const {
    const camel::runtime::GCNode *node = graph_.node(ref);
    return node == nullptr ? 0 : node->dataIndex;
}

const camel::core::type::Type *FrozenGraphRead::typeOf(gc_node_ref_t ref) const {
    const camel::runtime::GCNode *node = graph_.node(ref);
    return node == nullptr ? nullptr : node->dataType();
}

uint8_t FrozenGraphRead::flagsOf(gc_node_ref_t ref) const {
    const camel::runtime::GCNode *node = graph_.node(ref);
    return node == nullptr ? 0 : node->flags;
}

bool FrozenGraphRead::hasPayload(gc_node_ref_t ref) const {
    const camel::runtime::GCNode *node = graph_.node(ref);
    return node != nullptr && node->hasPayload();
}

/*
 * `firstDifference`, `describe`, `DraftGraphs` and `liveDraftNodes` were here and are gone: the
 * check over the face is `camel::graph::firstDifference` (kernel/graph/consistency.h), asked of the
 * view so that it compares what a reader of the view reads, and the two representations both answer
 * it. What was left of this unit is the face a lift still reads (plan 2.3 replaces it), and nothing
 * in it compares two graphs any more -- a second implementation of the check would be a second
 * answer.
 */
} // namespace camel::compile::gir
