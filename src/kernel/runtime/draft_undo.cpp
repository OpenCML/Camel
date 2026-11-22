// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The draft's undo journal: what a savepoint keeps, and what a rollback puts back.
 *
 * A draft is written in two ways that a count cannot take back. A segment appends nodes, and it
 * also edits the nodes that were already there -- a body, the list of readers of a body, the region
 * table, the graph's own scalars, its types, its port tables, the ids of the nodes it changed in
 * place. The first half is a size; the second is this file. Every writing entry that can change a
 * member in place records it once per segment, before it changes it (`noteOriginal*`, called by the
 * setters and by `rebuildNode`, which every in-place body write goes through), and `replayJournal`
 * walks the records back in the order they were taken. The journal lives here and not in `draft.cpp`
 * because it is a mechanism of its own -- one record per kind of state, one rule for when a record
 * is taken, one order for putting them back -- and because a defect in it is a defect of that
 * mechanism and not of the editing API that happens to call it.
 *
 * It also carries the journal's own audit: a whole copy of a draft taken at a savepoint, and the
 * comparison a rollback makes against it (see `UndoClass`). The journal hangs off one writing entry
 * per kind of state, and the defect that shape invites is a member written somewhere else -- a
 * write no rollback sees. Prose in a header cannot catch that. A copy taken at the mark can:
 * whatever the segment changed, the draft that comes back is compared with the draft that went in,
 * part by part, and a part that differs is named. The copy is the price, so it is taken only when
 * the self-check is on: a debug build's default, and a test that has to walk the classes
 * (`test::undo_selfcheck`).
 */

#include "camel/runtime/draft.h"

#include "camel/core/type/composite/func.h"

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

/// The result type a signature holds, or nullptr while it holds none yet: `exitType()` answers
/// `void` for both an open signature without a result and one whose result is `void`, and those two
/// are different states of the draft. The types are interned and immutable, so the pointer a
/// signature holds a result type by *is* that type's content, and comparing it compares what the
/// signature says rather than which object the draft points at.
camel::core::type::Type *exitTypeOf(const camel::core::type::FunctionType *signature) {
    if (signature == nullptr || !signature->hasExitType()) {
        return nullptr;
    }
    return signature->exitType();
}

/// The resources a signature publishes, as a value; Generic for a draft with no signature.
camel::core::sem::ResourceSignature
resourcesOf(const camel::core::type::FunctionType *signature) {
    return signature != nullptr ? signature->resources() : camel::core::sem::ResourceSignature{};
}

bool sameTypes(const GraphDraft &draft, const UndoSnapshot &snapshot) {
    // The signature is compared by what it *holds*, not only by which object it is. A segment that
    // fills an open signature writes it where it stands -- `funcType_` keeps pointing at the same
    // object -- so a comparison of the pointer finds the draft unchanged however much the signature
    // now says, and `test::undo_selfcheck` reports "back" for a signature that was never put back
    // ( the self-check compares content, not the pointer). The pointer still has
    // to match: a different one is a different signature, its parameters included.
    return draft.funcType() == snapshot.funcType &&
           exitTypeOf(draft.funcType()) == snapshot.exitType &&
           resourcesOf(draft.funcType()) == snapshot.resources &&
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
    snapshot->exitType         = exitTypeOf(draft.funcType());
    snapshot->resources        = resourcesOf(draft.funcType());
    snapshot->closureType      = draft.closureType();
    snapshot->captureNames.assign(draft.captureNames().begin(), draft.captureNames().end());
    snapshot->runtimeSlotTypes.assign(
        draft.runtimeSlotTypes().begin(), draft.runtimeSlotTypes().end());
    snapshot->staticSlotTypes.assign(draft.staticSlotTypes().begin(), draft.staticSlotTypes().end());
    snapshot->normPorts.assign(draft.normPorts().begin(), draft.normPorts().end());
    snapshot->withPorts.assign(draft.withPorts().begin(), draft.withPorts().end());
    snapshot->closureNodes.assign(draft.closureNodes().begin(), draft.closureNodes().end());
    snapshot->lowLevelEdits.assign(draft.lowLevelEdits().begin(), draft.lowLevelEdits().end());
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

    return std::nullopt;
}

GraphDraft::Savepoint GraphDraft::savepoint() const {
    // A mark taken outside a segment names no journal -- rolling back to it takes back the counts
    // and nothing else -- so the journal marks are the sizes the journal has now.
    Savepoint mark{
        .nodes          = nodesById_.size(),
        .staticSlots    = staticSlots_.size(),
        .frameSlots     = runtimeSlotTypes_.size(),
        .regions        = regions_.size(),
        .mutations      = mutations_,
        .hasFrameLayout = hasFrameLayout_,
        .undoNodes      = undoNodes_.size(),
        .undoRegions    = undoRegions_.size(),
        .undoScalars    = undoScalars_.size(),
        .undoTypes      = undoTypes_.size(),
        .undoPorts      = undoPorts_.size(),
        .undoLowLevel   = undoLowLevel_.size(),
    };
    // The self-check's copy is taken here, where every mark is made: what a rollback has to put
    // back is what the draft was at this instant, and a copy taken anywhere else would answer for
    // another state.
    if (selfCheck_ != SelfCheckMode::Off) {
        mark.check = takeUndoSnapshot(*this);
    }
    return mark;
}

GraphDraft::Savepoint GraphDraft::beginUndo() {
    ++journalDepth_;
    ++journalEpoch_;
    return savepoint();
}

GraphDraft::Savepoint GraphDraft::markUndo() {
    // A mark without a segment records nothing: a rollback to it is the count rollback, which is
    // what a caller outside `beginUndo` has always asked for. Inside one, the epoch moves so that
    // every kind the next application writes is recorded again -- the mark is a rollback point of
    // its own, and the first record after it is the state it found.
    if (journalOpen()) {
        ++journalEpoch_;
    }
    return savepoint();
}

void GraphDraft::endUndo() {
    if (journalDepth_ == 0) {
        return;
    }
    if (--journalDepth_ != 0) {
        return;
    }
    dropJournal();
}

void GraphDraft::dropJournal() {
    undoNodes_.clear();
    undoNodeEpoch_.clear();
    undoRegions_.clear();
    undoScalars_.clear();
    undoTypes_.clear();
    undoPorts_.clear();
    undoLowLevel_.clear();
    undoRegionsEpoch_  = 0;
    undoScalarsEpoch_  = 0;
    undoTypesEpoch_    = 0;
    undoPortsEpoch_    = 0;
    undoLowLevelEpoch_ = 0;
    journalEpoch_      = 0;
}

void GraphDraft::noteOriginalNode(gc_node_ref_t id) {
    if (!journalOpen() || undoSuspended_ || journalSkips(UndoClass::Nodes) ||
        id == kInvalidNodeRef) {
        return;
    }
    const DraftNode *draftNode = node(id);
    if (draftNode == nullptr) {
        // An erased node has no body left to record; its record was made before it was erased.
        return;
    }
    if (undoNodeEpoch_.size() <= id) {
        undoNodeEpoch_.resize(static_cast<size_t>(id) + 1, 0);
    }
    if (undoNodeEpoch_[id] == journalEpoch_) {
        return;
    }
    undoNodeEpoch_[id] = journalEpoch_;
    UndoNode record;
    record.id     = id;
    record.header = draftNode->header;
    const size_t used = draftNode->header.usedBytes();
    if (used != 0 && draftNode->tail != nullptr) {
        record.tail.assign(draftNode->tail, draftNode->tail + used);
    }
    // The user lists are recorded with the body, not on a second touch: a producer's list of
    // readers changes without the producer's own node being the thing the caller names, and a
    // record that had the body but not the lists could not put the graph back. A node with no list
    // yet records the empty one: "nobody reads it" is the state, not the absence of a state.
    record.usersRecorded = true;
    if (id < users_.size() && users_[id]) {
        for (size_t k = 0; k < 5; ++k) {
            record.users[k] = users_[id]->of(static_cast<DraftEdgeSlot>(k));
        }
    }
    undoNodes_.push_back(std::move(record));
}

void GraphDraft::noteOriginalRegions() {
    if (!journalOpen() || journalSkips(UndoClass::Regions) || undoRegionsEpoch_ == journalEpoch_) {
        return;
    }
    undoRegionsEpoch_ = journalEpoch_;
    undoRegions_.push_back(UndoRegions{.regions = regions_, .regionPorts = regionPorts_});
}

void GraphDraft::noteOriginalScalars() {
    if (!journalOpen() || journalSkips(UndoClass::Scalars) || undoScalarsEpoch_ == journalEpoch_) {
        return;
    }
    undoScalarsEpoch_ = journalEpoch_;
    undoScalars_.push_back(
        UndoScalars{
            .entry          = entry_,
            .exit           = exit_,
            .output         = output_,
            .returnNode     = returnNode_,
            .returnKind     = returnKind_,
            .mutations      = mutations_,
            .hasFrameLayout = hasFrameLayout_,
            .insertRegion   = insertRegion_,
        });
}

void GraphDraft::noteOriginalTypes() {
    if (!journalOpen() || journalSkips(UndoClass::Types) || undoTypesEpoch_ == journalEpoch_) {
        return;
    }
    undoTypesEpoch_ = journalEpoch_;
    undoTypes_.push_back(
        UndoTypes{
            .funcType           = funcType_,
            .exitType           = exitTypeOf(funcType_),
            .resources          = resourcesOf(funcType_),
            .closureType        = closureType_,
            .captureNames       = captureNames_,
            .runtimeSlotTypes   = runtimeSlotTypes_,
            .staticSlotTypes    = staticSlotTypes_,
        });
}

void GraphDraft::setOpenExitType(camel::core::type::Type *exitType) {
    // The record comes first and once per segment (the epoch rule `noteOriginal*` follows), so what
    // a rollback writes back is the result type the mark found and not one of the values the
    // segment wrote in between.
    noteOriginalTypes();
    camel::core::type::OpenFunctionType *open =
        funcType_ != nullptr ? funcType_->asOpen() : nullptr;
    GUARD(open != nullptr, "Only an open signature has a result type written in place.");
    open->setExitType(exitType);
}

void GraphDraft::setOpenResources(const camel::core::sem::ResourceSignature &resources) {
    noteOriginalTypes();
    camel::core::type::OpenFunctionType *open =
        funcType_ != nullptr ? funcType_->asOpen() : nullptr;
    GUARD(open != nullptr, "Only an open signature has its resources written in place.");
    open->setResources(resources);
}

void GraphDraft::noteOriginalPorts() {
    if (!journalOpen() || journalSkips(UndoClass::Ports) || undoPortsEpoch_ == journalEpoch_) {
        return;
    }
    undoPortsEpoch_ = journalEpoch_;
    undoPorts_.push_back(
        UndoPorts{.norm = normPorts_, .with = withPorts_, .closure = closureNodes_});
}

void GraphDraft::noteOriginalLowLevel() {
    if (!journalOpen() || journalSkips(UndoClass::LowLevel) || undoLowLevelEpoch_ == journalEpoch_) {
        return;
    }
    undoLowLevelEpoch_ = journalEpoch_;
    undoLowLevel_.push_back(UndoLowLevel{.edits = lowLevelEdits_.size()});
}

void GraphDraft::restoreUndoNode(UndoNode &record) {
    const gc_node_ref_t id = record.id;
    DraftNode *draftNode   = node(id);
    if (draftNode == nullptr) {
        // The id was never handed out twice, so a body put back at it is the same node and not a
        // reuse: no class, no edge and no recorded id has to move.
        const size_t capacity = static_cast<size_t>(record.header.capacityBytes);
        DraftNode *fresh      = pool_.allocNode();
        fresh->header         = record.header;
        fresh->header.owner   = this;
        fresh->header.selfId  = id;
        fresh->tail           = nullptr;
        if (capacity != 0) {
            DraftNodeStorageClass storageClass = record.header.storageCls;
            fresh->tail                        = pool_.allocTail(capacity, &storageClass);
            fresh->header.storageCls           = storageClass;
            fresh->header.capacityBytes        = static_cast<uint32_t>(capacity);
            std::memset(fresh->tail, 0, capacity);
        }
        if (nodesById_.size() <= id) {
            nodesById_.resize(static_cast<size_t>(id) + 1, nullptr);
        }
        nodesById_[id] = fresh;
        draftNode      = fresh;
        ++liveNodeCount_;
    } else {
        // The record was taken before the change that made the node grow, so its room is room the
        // node still has: the body goes back over what stands there, header and all.
        GUARD(
            record.header.capacityBytes <= draftNode->header.capacityBytes &&
                record.tail.size() <= record.header.capacityBytes,
            std::format(
                "restoring node {}: its record names {} tail byte(s) of {} of room, and the node "
                "has {}",
                id,
                record.tail.size(),
                record.header.capacityBytes,
                draftNode->header.capacityBytes));
        draftNode->header        = record.header;
        draftNode->header.owner  = this;
        draftNode->header.selfId = id;
    }
    if (!record.tail.empty()) {
        std::memcpy(draftNode->tail, record.tail.data(), record.tail.size());
    }
    // `replaceNodeStorage` is also what a body write ends with, so the slot is the node a reader
    // finds: the body, the counts and the class it sits in are the record's.
    replaceNodeStorage(id, draftNode);
    if (record.usersRecorded) {
        if (users_.size() <= id) {
            users_.resize(nodesById_.size());
        }
        if (!users_[id]) {
            users_[id] = std::make_unique<DraftUserLists>();
        }
        for (size_t k = 0; k < 5; ++k) {
            const auto slot = static_cast<DraftEdgeSlot>(k);
            users_[id]->of(slot) = std::move(record.users[k]);
            // The short-list index is a function of the list; a restored list makes the old one
            // an answer about a graph nothing holds any more.
            users_[id]->places[k].reset();
        }
    }
}

void GraphDraft::replayJournal(const Savepoint &mark) {
    // The whole-value records: the first one after the mark is the state the mark found, because
    // nothing of that kind was written between the mark and the record.
    if (undoRegions_.size() > mark.undoRegions) {
        const UndoRegions &record = undoRegions_[mark.undoRegions];
        regions_                  = record.regions;
        regionPorts_              = record.regionPorts;
    }
    if (undoScalars_.size() > mark.undoScalars) {
        const UndoScalars &record = undoScalars_[mark.undoScalars];
        entry_                    = record.entry;
        exit_                     = record.exit;
        output_                   = record.output;
        returnNode_               = record.returnNode;
        returnKind_               = record.returnKind;
        mutations_                = record.mutations;
        hasFrameLayout_           = record.hasFrameLayout;
        insertRegion_             = record.insertRegion;
    }
    if (undoPorts_.size() > mark.undoPorts) {
        const UndoPorts &record = undoPorts_[mark.undoPorts];
        normPorts_              = record.norm;
        withPorts_              = record.with;
        closureNodes_           = record.closure;
    }
    if (undoTypes_.size() > mark.undoTypes) {
        const UndoTypes &record = undoTypes_[mark.undoTypes];
        funcType_               = record.funcType;
        // The signature goes back to the object the record names, and that object goes back to what
        // it held: a segment fills an open signature where it stands, so putting the pointer back
        // leaves the write in place -- the result type the segment wrote and the resources with it.
        // An open signature is the one kind that can be written in place (the type table never holds
        // one), so it is the one with content to write back. The two writes go
        // to the signature and not through `setOpenExitType` / `setOpenResources`: a restore is not
        // a write of the segment, and the recording entries do not check `undoSuspended_`, so going
        // through them would record the state this rollback is leaving.
        if (camel::core::type::OpenFunctionType *open =
                funcType_ != nullptr ? funcType_->asOpen() : nullptr) {
            open->setExitType(record.exitType);
            open->setResources(record.resources);
        }
        closureType_            = record.closureType;
        captureNames_           = record.captureNames;
        runtimeSlotTypes_       = record.runtimeSlotTypes;
        staticSlotTypes_        = record.staticSlotTypes;
    }
    if (undoLowLevel_.size() > mark.undoLowLevel) {
        // The ids the segment put in the edit list go with the edits themselves: a node the
        // rollback put back is not a node a commit has to answer for as changed in place.
        const size_t base = undoLowLevel_[mark.undoLowLevel].edits;
        for (size_t at = base; at < lowLevelEdits_.size(); ++at) {
            const gc_node_ref_t id = lowLevelEdits_[at];
            if (id < lowLevelSeen_.size()) {
                lowLevelSeen_[id] = 0;
            }
        }
        if (lowLevelEdits_.size() > base) {
            lowLevelEdits_.resize(base);
        }
    }
    undoRegions_.resize(mark.undoRegions);
    undoScalars_.resize(mark.undoScalars);
    undoTypes_.resize(mark.undoTypes);
    undoPorts_.resize(mark.undoPorts);
    undoLowLevel_.resize(mark.undoLowLevel);
    undoRegionsEpoch_  = 0;
    undoScalarsEpoch_  = 0;
    undoTypesEpoch_    = 0;
    undoPortsEpoch_    = 0;
    undoLowLevelEpoch_ = 0;
    // The nodes last: a restored body names inputs, and the record below a mark is the one that
    // holds the lists the segment changed.
    for (size_t at = undoNodes_.size(); at > mark.undoNodes; --at) {
        UndoNode &record = undoNodes_[at - 1];
        if (record.id < undoNodeEpoch_.size()) {
            undoNodeEpoch_[record.id] = 0;
        }
        // A body recorded for a node the segment appended goes with the node: putting it back would
        // be a node that the count rollback above already removed, at an id the graph no longer
        // reaches.
        if (record.id >= mark.nodes) {
            continue;
        }
        restoreUndoNode(record);
    }
    undoNodes_.resize(mark.undoNodes);
}

void GraphDraft::rollbackTo(const Savepoint &mark) {
    requireOneWriting("rollbackTo");
    // What a savepoint can take back is what was appended and what the journal recorded. A region
    // is not appended by a rule's right side -- it is spliced into regions that exist -- so a
    // mark whose segment recorded no region table must still find the count it named.
    GUARD(
        mark.nodes <= nodesById_.size() && mark.staticSlots <= staticSlots_.size() &&
            mark.frameSlots <= runtimeSlotTypes_.size(),
        std::format(
            "rollbackTo: the savepoint names {} node(s), {} static slot(s) and {} frame slot(s), "
            "and the draft has {} / {} / {}",
            mark.nodes,
            mark.staticSlots,
            mark.frameSlots,
            nodesById_.size(),
            staticSlots_.size(),
            runtimeSlotTypes_.size()));
    // Newest first, because a node the application built reads the ones built before it and an
    // erasure unlinks its readers. Nothing outside the run reads any of them -- a right side
    // appends nodes, it edits none -- so the run empties from the far end. The erasures themselves
    // are not recorded: this is a rollback, so what they would record is what the journal already
    // says, and a record taken here would be the state the segment *left*.
    {
        const bool wasSuspended = undoSuspended_;
        undoSuspended_          = true;
        for (size_t at = nodesById_.size(); at > mark.nodes; --at) {
            const gc_node_ref_t id = static_cast<gc_node_ref_t>(at - 1);
            if (node(id) != nullptr) {
                eraseNode(id);
            }
        }
        undoSuspended_ = wasSuspended;
    }
    // The half the counts cannot take back: the bodies, the user lists and the tables the segment
    // changed in place.
    replayJournal(mark);
    // The slots come back with it. The ids do not: `addNode` hands out `nodesById_.size()` and
    // never gives one to a second node, and the id lists that keep erased nodes (`gateIds_`,
    // `brchIds_`, `joinIds_`) skip them rather than forget them, so the id space stays spent.
    staticSlots_.resize(mark.staticSlots);
    staticSlotTypes_.resize(mark.staticSlots);
    runtimeSlotTypes_.resize(mark.frameSlots);
    hasFrameLayout_ = mark.hasFrameLayout;
    // The commit reads a node's own edits as the second half of its tier (`lowLevelEdits`): the ids
    // of the nodes the run wrote to go with the nodes themselves, or a commit would answer for a
    // node that is not in the graph it publishes.
    std::erase_if(lowLevelEdits_, [&](gc_node_ref_t id) { return id >= mark.nodes; });
    if (lowLevelSeen_.size() > mark.nodes) {
        lowLevelSeen_.resize(mark.nodes);
    }
    // The draft is what the mark found, down to the count the commit reads as "something has been
    // written since the last delivery" (draft_session.cpp): an application that was abandoned wrote
    // nothing, and a commit that re-encoded the graph for it would publish a version nothing
    // changed.
    mutations_ = mark.mutations;
    GUARD(
        regions_.size() == mark.regions,
        std::format(
            "rollbackTo: the savepoint names {} region(s) and the draft has {} after the journal "
            "was replayed; a segment that adds a region must record the table before it does",
            mark.regions,
            regions_.size()));
    if (nodesById_.size() > mark.nodes) {
        // The draft was written to and is now back where it was: `touchRefs` is what tells a reader
        // that the shape it read is stale, and the erasures inside the loop do not all do it.
        touchRefs();
    }
    // The audit, last, when the mark carries a copy of the draft: the classes are compared one by
    // one, in the order the journal writes them, and the first that is not back is the answer. A
    // hook that was forgotten is exactly a class the segment wrote and the journal did not keep, so
    // this is what makes such a hook red in the test that walks the path -- and, in the mode a
    // debug build starts in, an invariant rather than a line of output.
    lastMismatch_.reset();
    if (mark.check) {
        lastMismatch_ = camel::runtime::undoMismatch(*this, *mark.check);
        if (lastMismatch_) {
            const std::string reading = std::format(
                "the {} did not come back from the rollback (mark at {} node(s), draft at {}); a "
                "write of that class went round its hook",
                undoClassName(*lastMismatch_),
                mark.nodes,
                nodesById_.size());
            if (selfCheck_ == SelfCheckMode::Abort) {
                ::cml::fail_invariant("rollbackTo leaves the draft the mark found", reading);
            }
        }
    }
}

void GraphDraft::noteLowLevelEdit(gc_node_ref_t id) {
    // A named edit is the edit: what it changes in place is that edit's, and the table of
    // docs/design/graph-editing/README.cn.md 8.1 owes the effect and order checks nothing for it.
    if (namedEdits_ != 0 || id == kInvalidNodeRef) {
        return;
    }
    if (id >= lowLevelSeen_.size()) {
        lowLevelSeen_.resize(static_cast<size_t>(id) + 1, 0);
    }
    if (lowLevelSeen_[id] != 0) {
        return;
    }
    lowLevelSeen_[id] = 1;
    // The list the segment is about to grow, as it stood: a rolled back edit takes its id back
    // out, or the commit would answer for a change that is not in the graph it publishes.
    noteOriginalLowLevel();
    lowLevelEdits_.push_back(id);
}

} // namespace camel::runtime
