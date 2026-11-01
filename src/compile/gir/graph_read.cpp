// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/compile/gir/graph_read.h"

#include <cstring>
#include <format>
#include <iostream>

namespace camel::compile::gir {
namespace {

/// Reads a payload body off a draft's tail. A draft keeps a node's payload as bytes at an
/// arbitrary offset, so the body is copied out instead of reinterpreted in place: the copy is what
/// makes this safe for a body with pointers in it (`GCOperBody`, `GCFuncRef`).
template <typename Body>
bool draftBodyOf(
    const camel::runtime::GraphDraft &draft, gc_node_ref_t ref, Body &out) {
    const std::span<const std::byte> payload = draft.payloadOf(ref);
    if (payload.size() < sizeof(Body)) {
        return false;
    }
    std::memcpy(&out, payload.data(), sizeof(Body));
    return true;
}

/// One letter per kind, so a reading can print a graph's node kinds in node id order on one line
/// without a table of words.
char kindLetter(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data: return 'D';
    case GCNodeKind::Port: return 'P';
    case GCNodeKind::Cast: return 'C';
    case GCNodeKind::Copy: return 'Y';
    case GCNodeKind::Fill: return 'F';
    case GCNodeKind::Accs: return 'A';
    case GCNodeKind::Brch: return 'B';
    case GCNodeKind::Join: return 'J';
    case GCNodeKind::Call: return 'L';
    case GCNodeKind::Bind: return 'N';
    case GCNodeKind::Func: return 'U';
    case GCNodeKind::Oper: return 'O';
    case GCNodeKind::Gate: return 'G';
    case GCNodeKind::Dref: return 'E';
    case GCNodeKind::Loop: return 'W';
    case GCNodeKind::Dead: return '_';
    }
    return '?';
}

std::string kindsLine(const GraphRead &graph) {
    std::string line;
    line.reserve(static_cast<size_t>(graph.nodeSlotCount()));
    for (gc_node_ref_t ref = 0; ref < graph.nodeSlotCount(); ++ref) {
        line.push_back(graph.alive(ref) ? kindLetter(graph.kindOf(ref)) : '.');
    }
    return line;
}

/// The disagreement, when two input lists of the same node are not the same list.
std::string listDifference(
    gc_node_ref_t ref, const char *role, const std::vector<gc_node_ref_t> &draft,
    const std::vector<gc_node_ref_t> &frozen) {
    if (draft.size() != frozen.size()) {
        return std::format(
            "node {} {} inputs: draft has {}, frozen has {}",
            ref,
            role,
            draft.size(),
            frozen.size());
    }
    for (size_t i = 0; i < draft.size(); ++i) {
        if (draft[i] != frozen[i]) {
            return std::format(
                "node {} {} input {}: draft has {}, frozen has {}",
                ref,
                role,
                i,
                draft[i],
                frozen[i]);
        }
    }
    return {};
}

} // namespace

bool DraftGraphRead::alive(gc_node_ref_t ref) const {
    return kept_.empty() ? draft_.alive(ref) : ref < kept_.size();
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
    return mappedListOf(draft_.normInputsOf(sourceIdOf(ref)));
}

std::vector<gc_node_ref_t> DraftGraphRead::withInputsOf(gc_node_ref_t ref) const {
    return mappedListOf(draft_.withInputsOf(sourceIdOf(ref)));
}

std::vector<gc_node_ref_t> DraftGraphRead::ctrlInputsOf(gc_node_ref_t ref) const {
    return mappedListOf(draft_.ctrlInputsOf(sourceIdOf(ref)));
}

GCNodeKind DraftGraphRead::kindOf(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = draft_.header(sourceIdOf(ref));
    return header == nullptr ? GCNodeKind::Dead : header->kind;
}

gc_slot_idx_t DraftGraphRead::dataIndexOf(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = draft_.header(sourceIdOf(ref));
    return header == nullptr ? 0 : header->dataIndex;
}

const camel::core::type::Type *DraftGraphRead::typeOf(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = draft_.header(sourceIdOf(ref));
    return header == nullptr ? nullptr : header->dataType;
}

uint8_t DraftGraphRead::flagsOf(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = draft_.header(sourceIdOf(ref));
    return header == nullptr ? 0 : header->runtimeFlags;
}

bool DraftGraphRead::hasPayload(gc_node_ref_t ref) const {
    const camel::runtime::DraftNodeHeader *header = draft_.header(sourceIdOf(ref));
    return header != nullptr && header->payloadBytes != 0;
}

GraphId DraftGraphRead::calleeIdOf(gc_node_ref_t ref) const {
    if (kindOf(ref) != GCNodeKind::Func) {
        return camel::runtime::kInvalidGraphId;
    }
    const std::span<const std::byte> payload = draft_.payloadOf(sourceIdOf(ref));
    if (payload.size() < sizeof(camel::runtime::GCFuncRef)) {
        return camel::runtime::kInvalidGraphId;
    }
    camel::runtime::GCFuncRef body{};
    std::memcpy(&body, payload.data(), sizeof(body));
    const std::span<const GraphId> callees = draft_.payloadGraphs();
    return body.ref < callees.size() ? callees[body.ref] : camel::runtime::kInvalidGraphId;
}

std::string DraftGraphRead::operUriOf(gc_node_ref_t ref) const {
    if (kindOf(ref) != GCNodeKind::Oper) {
        return {};
    }
    camel::runtime::GCOperBody body{};
    if (!draftBodyOf(draft_, sourceIdOf(ref), body)) {
        return {};
    }
    return std::string(body.uri());
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

std::string FrozenGraphRead::operUriOf(gc_node_ref_t ref) const {
    if (kindOf(ref) != GCNodeKind::Oper) {
        return {};
    }
    const auto *body = graph_.nodeBodyAs<camel::runtime::GCOperBody>(ref);
    return body == nullptr ? std::string{} : std::string(body->uri());
}

std::string firstDifference(const GraphRead &draft, const GraphRead &frozen) {
    if (draft.nodeSlotCount() != frozen.nodeSlotCount()) {
        return std::format(
            "node slots: draft has {}, frozen has {}",
            draft.nodeSlotCount(),
            frozen.nodeSlotCount());
    }
    if (draft.liveNodeCount() != frozen.liveNodeCount()) {
        return std::format(
            "live nodes: draft has {}, frozen has {}",
            draft.liveNodeCount(),
            frozen.liveNodeCount());
    }
    for (gc_node_ref_t ref = 0; ref < draft.nodeSlotCount(); ++ref) {
        if (draft.alive(ref) != frozen.alive(ref)) {
            return std::format(
                "node {}: alive draft={} frozen={}", ref, draft.alive(ref), frozen.alive(ref));
        }
        if (!draft.alive(ref)) {
            continue;
        }
        if (draft.kindOf(ref) != frozen.kindOf(ref)) {
            return std::format(
                "node {}: kind draft={} frozen={}",
                ref,
                static_cast<int>(draft.kindOf(ref)),
                static_cast<int>(frozen.kindOf(ref)));
        }
        // A data index is compared for a Data node only: that is the node whose index names its
        // value's place in the graph's data area. For another kind the index is the writer's own
        // bookkeeping -- a draft keeps what the builder set, the encoder writes the graph's "no
        // slot" value -- and it is not a fact about the graph a reader of the face can use.
        if (draft.kindOf(ref) == GCNodeKind::Data &&
            draft.dataIndexOf(ref) != frozen.dataIndexOf(ref)) {
            return std::format(
                "node {}: data index draft={} frozen={}",
                ref,
                draft.dataIndexOf(ref),
                frozen.dataIndexOf(ref));
        }
        // A type is compared by identity first and by spelling second. The spelling is the second
        // half because a graph's own function type is an object of that graph (each lambda and each
        // instance has one) while a frozen graph keeps an interned id, and the encoder writes the id
        // of the type with the same shape: two objects that print the same are the same type here,
        // and a type that prints differently is a difference whatever its pointer.
        const camel::core::type::Type *draftType = draft.typeOf(ref);
        const camel::core::type::Type *frozenType = frozen.typeOf(ref);
        if (draftType != frozenType &&
            (draftType == nullptr || frozenType == nullptr ||
             draftType->toString() != frozenType->toString())) {
            return std::format(
                "node {}: type draft={} frozen={}",
                ref,
                draftType == nullptr ? "<none>" : draftType->toString(),
                frozenType == nullptr ? "<none>" : frozenType->toString());
        }
        // Only the two flags this face answers are compared; a draft's byte carries the same bits
        // the encoder writes to the frozen node's column.
        constexpr uint8_t kComparedFlags =
            camel::runtime::kGCNodeFlagMacro | camel::runtime::kGCNodeFlagConstant;
        if ((draft.flagsOf(ref) & kComparedFlags) != (frozen.flagsOf(ref) & kComparedFlags)) {
            return std::format(
                "node {}: flags draft={} frozen={}",
                ref,
                static_cast<int>(draft.flagsOf(ref)),
                static_cast<int>(frozen.flagsOf(ref)));
        }
        if (draft.hasPayload(ref) != frozen.hasPayload(ref)) {
            return std::format(
                "node {}: payload draft={} frozen={}",
                ref,
                draft.hasPayload(ref),
                frozen.hasPayload(ref));
        }
        if (std::string role;
            !(role = listDifference(
                  ref, "norm", draft.normInputsOf(ref), frozen.normInputsOf(ref)))
                 .empty()) {
            return role;
        }
        if (std::string role;
            !(role = listDifference(
                  ref, "with", draft.withInputsOf(ref), frozen.withInputsOf(ref)))
                 .empty()) {
            return role;
        }
        if (std::string role;
            !(role = listDifference(
                  ref, "ctrl", draft.ctrlInputsOf(ref), frozen.ctrlInputsOf(ref)))
                 .empty()) {
            return role;
        }
        if (draft.kindOf(ref) == GCNodeKind::Func &&
            draft.calleeIdOf(ref) != frozen.calleeIdOf(ref)) {
            return std::format(
                "node {}: callee draft={} frozen={}",
                ref,
                camel::runtime::toValue(draft.calleeIdOf(ref)),
                camel::runtime::toValue(frozen.calleeIdOf(ref)));
        }
        if (draft.kindOf(ref) == GCNodeKind::Oper) {
            const std::string oper = draft.operUriOf(ref);
            const std::string other = frozen.operUriOf(ref);
            if (oper != other) {
                return std::format("node {}: oper draft={} frozen={}", ref, oper, other);
            }
        }
    }
    return {};
}


void DraftGraphs::add(const camel::runtime::GraphDraft &draft) {
    byId_.try_emplace(draft.id(), draft);
}

const GraphRead *DraftGraphs::byId(GraphId id) const {
    const auto found = byId_.find(id);
    return found == byId_.end() ? nullptr : &found->second;
}

std::string describe(const GraphRead &graph) {
    std::string calls;
    std::string opers;
    for (gc_node_ref_t ref = 0; ref < graph.nodeSlotCount(); ++ref) {
        if (!graph.alive(ref)) {
            continue;
        }
        if (graph.kindOf(ref) == GCNodeKind::Func) {
            calls += (calls.empty() ? "" : ",") + camel::runtime::toText(graph.calleeIdOf(ref));
        } else if (graph.kindOf(ref) == GCNodeKind::Oper) {
            opers += (opers.empty() ? "" : ",") + graph.operUriOf(ref);
        }
    }
    return std::format(
        "nodes={} kinds={} calls={} opers={}",
        graph.liveNodeCount(),
        kindsLine(graph),
        calls.empty() ? "-" : calls,
        opers.empty() ? "-" : opers);
}

} // namespace camel::compile::gir