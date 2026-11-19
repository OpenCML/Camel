// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The undo journal's audit: a whole copy of a draft taken at a savepoint, and the comparison a
 * rollback makes against it (see `GraphDraft::rollbackTo` and `UndoClass`).
 *
 * The journal hangs off one writing entry per kind of state, and the defect it invites is a member
 * written somewhere else -- a write no rollback sees. Prose in a header cannot catch that. A copy
 * taken at the mark can: whatever the segment changed, the draft that comes back is compared with
 * the draft that went in, part by part, and a part that differs is named. The copy is the price,
 * so it is taken only when the self-check is on: a debug build's default, and a test that has to
 * walk the classes.
 */

#include "camel/runtime/draft.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <optional>
#include <string_view>

namespace camel::runtime {

namespace {

/// The header, field for field. A comparison over `memcmp` would read padding the compiler is free
/// to leave anything in, and a comparison over a hash would hide which node moved.
bool sameHeader(const DraftNodeHeader &a, const DraftNodeHeader &b) {
    return a.owner == b.owner && a.selfId == b.selfId && a.dataIndex == b.dataIndex &&
           a.payloadBytes == b.payloadBytes && a.capacityBytes == b.capacityBytes &&
           a.dataType == b.dataType && a.normInputCount == b.normInputCount &&
           a.withInputCount == b.withInputCount && a.ctrlInputCount == b.ctrlInputCount &&
           a.orderInputCount == b.orderInputCount && a.syncInputCount == b.syncInputCount &&
           a.kind == b.kind && a.runtimeFlags == b.runtimeFlags && a.tangentArgs == b.tangentArgs &&
           a.storageCls == b.storageCls && a.permissions == b.permissions && a.attrs == b.attrs &&
           a.name == b.name && a.origin == b.origin && a.region == b.region;
}

bool sameBytes(std::span<const std::byte> a, const std::vector<std::byte> &b) {
    if (a.size() != b.size()) {
        return false;
    }
    return a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0;
}

/// The five reader lists of `id`, in the order `UndoSnapshotNode::users` holds them.
std::array<std::span<const gc_node_ref_t>, 5> userListsOf(const GraphDraft &draft, gc_node_ref_t id) {
    return {
        draft.normUsersOf(id),
        draft.withUsersOf(id),
        draft.ctrlUsersOf(id),
        draft.orderUsersOf(id),
        draft.syncUsersOf(id),
    };
}

bool sameNodes(const GraphDraft &draft, const UndoSnapshot &snapshot) {
    if (draft.nodeSlotCount() != snapshot.nodes.size()) {
        return false;
    }
    for (gc_node_ref_t id = 0; id < static_cast<gc_node_ref_t>(snapshot.nodes.size()); ++id) {
        const UndoSnapshotNode &found  = snapshot.nodes[id];
        const DraftNodeHeader *header = draft.header(id);
        if ((header != nullptr) != found.present) {
            return false;
        }
        if (header == nullptr) {
            continue;
        }
        if (!sameHeader(*header, found.header) || !sameBytes(draft.payloadOf(id), found.tail)) {
            return false;
        }
        const auto lists = userListsOf(draft, id);
        for (size_t k = 0; k < lists.size(); ++k) {
            if (lists[k].size() != found.users[k].size() ||
                !std::equal(lists[k].begin(), lists[k].end(), found.users[k].begin())) {
                return false;
            }
        }
    }
    return true;
}

bool sameRegions(const GraphDraft &draft, const UndoSnapshot &snapshot) {
    if (draft.regionCount() != snapshot.regions.size()) {
        return false;
    }
    for (uint32_t id = 0; id < static_cast<uint32_t>(snapshot.regions.size()); ++id) {
        const GCRegion &found = snapshot.regions[id];
        const GCRegion &here  = draft.region(id);
        if (here.kind != found.kind || here.reserved0 != found.reserved0 ||
            here.reserved1 != found.reserved1 || here.parent != found.parent ||
            here.owner != found.owner || here.index != found.index || here.result != found.result ||
            here.portBegin != found.portBegin || here.portCount != found.portCount ||
            here.origin != found.origin || here.attrs != found.attrs ||
            here.inlineOf != found.inlineOf || here.callSite != found.callSite ||
            here.params != found.params) {
            return false;
        }
        const auto ports = draft.regionPortsOf(id);
        if (ports.size() != snapshot.regionPorts[id].size() ||
            !std::equal(ports.begin(), ports.end(), snapshot.regionPorts[id].begin())) {
            return false;
        }
    }
    return true;
}

bool sameScalars(const GraphDraft &draft, const UndoSnapshot &snapshot) {
    return draft.entryNode() == snapshot.entry && draft.exitNode() == snapshot.exit &&
           draft.outputNode() == snapshot.output && draft.returnNode() == snapshot.returnNode &&
           draft.returnKind() == snapshot.returnKind && draft.mutations() == snapshot.mutations &&
           draft.hasFrameLayout() == snapshot.hasFrameLayout &&
           draft.insertRegion() == snapshot.insertRegion;
}

template<typename T, typename U>
bool sameRun(const T &here, const std::vector<U> &found) {
    return here.size() == found.size() && std::equal(here.begin(), here.end(), found.begin());
}

bool sameTypes(const GraphDraft &draft, const UndoSnapshot &snapshot) {
    return draft.funcType() == snapshot.funcType &&
           draft.declaredFuncType() == snapshot.declaredFuncType &&
           draft.closureType() == snapshot.closureType &&
           sameRun(draft.captureNames(), snapshot.captureNames) &&
           sameRun(draft.runtimeSlotTypes(), snapshot.runtimeSlotTypes) &&
           sameRun(draft.staticSlotTypes(), snapshot.staticSlotTypes);
}

bool samePorts(const GraphDraft &draft, const UndoSnapshot &snapshot) {
    return sameRun(draft.normPorts(), snapshot.normPorts) &&
           sameRun(draft.withPorts(), snapshot.withPorts) &&
           sameRun(draft.closureNodes(), snapshot.closureNodes);
}

} // namespace

std::shared_ptr<const UndoSnapshot> takeUndoSnapshot(const GraphDraft &draft) {
    auto snapshot      = std::make_shared<UndoSnapshot>();
    const size_t slots = draft.nodeSlotCount();
    snapshot->nodes.resize(slots);
    for (size_t at = 0; at < slots; ++at) {
        const gc_node_ref_t id        = static_cast<gc_node_ref_t>(at);
        const DraftNodeHeader *header = draft.header(id);
        if (header == nullptr) {
            continue;
        }
        UndoSnapshotNode &found = snapshot->nodes[at];
        found.present           = true;
        found.header            = *header;
        const auto payload      = draft.payloadOf(id);
        found.tail.assign(payload.begin(), payload.end());
        const auto lists = userListsOf(draft, id);
        for (size_t k = 0; k < lists.size(); ++k) {
            found.users[k].assign(lists[k].begin(), lists[k].end());
        }
    }
    snapshot->regions.reserve(draft.regionCount());
    snapshot->regionPorts.reserve(draft.regionCount());
    const uint32_t regionCount = static_cast<uint32_t>(draft.regionCount());
    for (uint32_t id = 0; id < regionCount; ++id) {
        snapshot->regions.push_back(draft.region(id));
        const auto ports = draft.regionPortsOf(id);
        snapshot->regionPorts.emplace_back(ports.begin(), ports.end());
    }
    snapshot->entry          = draft.entryNode();
    snapshot->exit           = draft.exitNode();
    snapshot->output         = draft.outputNode();
    snapshot->returnNode     = draft.returnNode();
    snapshot->returnKind     = draft.returnKind();
    snapshot->mutations      = draft.mutations();
    snapshot->hasFrameLayout = draft.hasFrameLayout();
    snapshot->insertRegion   = draft.insertRegion();
    snapshot->funcType         = draft.funcType();
    snapshot->declaredFuncType = draft.declaredFuncType();
    snapshot->closureType      = draft.closureType();
    snapshot->captureNames.assign(draft.captureNames().begin(), draft.captureNames().end());
    snapshot->runtimeSlotTypes.assign(
        draft.runtimeSlotTypes().begin(), draft.runtimeSlotTypes().end());
    snapshot->staticSlotTypes.assign(draft.staticSlotTypes().begin(), draft.staticSlotTypes().end());
    snapshot->normPorts.assign(draft.normPorts().begin(), draft.normPorts().end());
    snapshot->withPorts.assign(draft.withPorts().begin(), draft.withPorts().end());
    snapshot->closureNodes.assign(draft.closureNodes().begin(), draft.closureNodes().end());
    snapshot->lowLevelEdits.assign(draft.lowLevelEdits().begin(), draft.lowLevelEdits().end());
    snapshot->writings.assign(draft.writings().begin(), draft.writings().end());
    return snapshot;
}

std::optional<UndoClass> undoMismatch(const GraphDraft &draft, const UndoSnapshot &snapshot) {
    // The order the classes are compared in is the order they are reported in, and it is the
    // journal's own: a part whose restore is missing is named before the parts a restore of it
    // would have carried along (a port that comes back but a signature that does not is a port
    // that is back, so the port is asked first).
    if (!sameNodes(draft, snapshot)) {
        return UndoClass::Nodes;
    }
    if (!sameRegions(draft, snapshot)) {
        return UndoClass::Regions;
    }
    if (!sameScalars(draft, snapshot)) {
        return UndoClass::Scalars;
    }
    if (!samePorts(draft, snapshot)) {
        return UndoClass::Ports;
    }
    if (!sameTypes(draft, snapshot)) {
        return UndoClass::Types;
    }
    if (!sameRun(draft.lowLevelEdits(), snapshot.lowLevelEdits)) {
        return UndoClass::LowLevel;
    }
    if (!sameRun(draft.writings(), snapshot.writings)) {
        return UndoClass::Writings;
    }
    return std::nullopt;
}

} // namespace camel::runtime