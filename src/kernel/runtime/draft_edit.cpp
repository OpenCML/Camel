// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The edit record of a draft, and the two entries that write it.
 *
 * Docs/design/graph-editing/README.cn.md 8.1 is the table this file makes a fact: writing a graph
 * has two entries -- the named edit operators (`replace`, `erase`, `eval`, `inline`, `outline`,
 * `order`, a parameter port, the exit value) and the kernel's primitives (a node added, an operand
 * added or taken away, an operator swapped) -- and both owe the same three things:
 *
 *   1. a record of the kind of edit and of the regions it changed (`recordEdit`, fed by
 *      `GraphDraft::NamedEdit` on the high side and by `noteEditedNode` on the low side);
 *   2. a notification to whatever is watching the draft (`EditListener`), once per edit, so that a
 *      work queue, the facts a session keeps and the commit's own check are told rather than left
 *      to ask again;
 *   3. a tier at publish, read off that record (`editTier`): which of the six checks the kinds the
 *      draft used can have broken.
 *
 * Both entries land here instead of each keeping its own list because an entry that kept its own
 * would be one an edit could go round: the primitives are what the named operators are written
 * with, so the record has to be made where the primitive writes, and the named edit has to be able
 * to say "these were mine" without writing a second history.
 */

#include "camel/runtime/draft.h"

#include <algorithm>
#include <format>

namespace camel::runtime {

std::string_view editKindName(EditKind kind) {
    switch (kind) {
    case EditKind::Replace:
        return "replace";
    case EditKind::Erase:
        return "erase";
    case EditKind::Eval:
        return "eval";
    case EditKind::Inline:
        return "inline";
    case EditKind::Outline:
        return "outline";
    case EditKind::Order:
        return "order";
    case EditKind::ParamPort:
        return "param_port";
    case EditKind::ExitValue:
        return "exit_value";
    case EditKind::LowLevel:
        return "low_level";
    }
    return "low_level";
}

namespace {

/// Appends `region` to a union kept ascending and without repetition. The set is small -- a graph
/// has as many regions as the program has branches, loops and arms -- so an insert is the right
/// cost, and `editedRegions` then answers in the order a reader wants to see it.
void addToUnion(std::vector<uint32_t> &unionOf, uint32_t region) {
    const auto at = std::lower_bound(unionOf.begin(), unionOf.end(), region);
    if (at == unionOf.end() || *at != region) {
        unionOf.insert(at, region);
    }
}

} // namespace

void GraphDraft::recordEdit(EditKind kind, std::span<const uint32_t> regions) {
    // A rollback replays the journal by writing bodies and tables back; that is not an edit of the
    // graph and must not appear in the record a commit reads.
    if (undoSuspended_) {
        return;
    }
    // The record as the segment found it, before the first note goes in: a rollback has to take the
    // notes of the edits it takes back (`UndoLowLevel::notes`). Taken here and not in
    // `noteEditedNode`, because a named edit writes its notes when it closes and may have written
    // none of the low-level list.
    noteOriginalLowLevel();
    ++editCount_;
    editKinds_ |= static_cast<uint32_t>(1) << static_cast<uint32_t>(kind);
    for (uint32_t region : regions) {
        editNotes_.push_back(EditNote{.kind = kind, .region = region});
    }
    // The listeners are told once per edit and not once per region: the edit is the unit both
    // entries work in, and a listener that had to fold several calls back together would be
    // rebuilding the record this type already has.
    for (EditListener *listener : editListeners_) {
        listener->onEditRecorded(kind, regions);
    }
}

void GraphDraft::noteEditedNode(gc_node_ref_t id, bool inPlace) {
    if (id == kInvalidNodeRef) {
        return;
    }
    if (namedEdits_ != 0) {
        // The named edit open now is the edit: its regions are the union of what its primitives
        // touched, and it records once when it closes.
        const DraftNode *changed = node(id);
        if (changed != nullptr) {
            addToUnion(namedEditRegions_, changed->header.region);
        }
        return;
    }
    if (inPlace) {
        if (id >= lowLevelSeen_.size()) {
            lowLevelSeen_.resize(static_cast<size_t>(id) + 1, 0);
        }
        if (lowLevelSeen_[id] == 0) {
            lowLevelSeen_[id] = 1;
            // The list the segment is about to grow, as it stood: a rolled back edit takes its id
            // back out, or the commit would answer for a change that is not in the graph it
            // publishes.
            noteOriginalLowLevel();
            lowLevelEdits_.push_back(id);
        }
    }
    const DraftNode *changed = node(id);
    if (changed == nullptr) {
        // An erased node is gone from the draft by the time its region would be read; the callers
        // that erase record the region before they remove the body (`eraseNode`).
        return;
    }
    const uint32_t region = changed->header.region;
    recordEdit(EditKind::LowLevel, std::span<const uint32_t>(&region, 1));
}

bool GraphDraft::editedWith(EditKind kind) const {
    return (editKinds_ & (static_cast<uint32_t>(1) << static_cast<uint32_t>(kind))) != 0;
}

std::vector<uint32_t> GraphDraft::editedRegions() const {
    std::vector<uint32_t> unionOf;
    unionOf.reserve(editNotes_.size());
    for (const EditNote &note : editNotes_) {
        addToUnion(unionOf, note.region);
    }
    return unionOf;
}

EditTier GraphDraft::editTier() const {
    EditTier tier;
    // The table's two right-hand columns, per row. The four structural checks are asked of every
    // commit and are not here; these two are the ones a row can answer by theorem.
    //
    //   `replace`, `erase`: the edit re-points readers and re-sequences what it removes, so the
    //                       effect and the ordering it leaves are the ones it left.
    //   `eval`:             the node becomes a constant; a constant carries no access.
    //   `order`:            an order edge makes nothing run, so it adds no access and leaves every
    //                       pair it did not name as it was.
    //   `param_port`, `exit_value`: the same two answers as `replace`, which is how both are
    //                       written.
    //   `outline`:          it takes accesses out of the body rather than putting them in, so it
    //                       cannot leave the body reaching past its type; the pairs it leaves are
    //                       read by the order question.
    //   `inline`:           a splice writes a callee's accesses into a region, which is exactly what
    //                       both questions are about; the record keeps the kind and the commit's own
    //                       reading narrows it to the callees that carry an access.
    //   `low_level`:        the last row, which owes all six.
    //
    // Which kinds a record can name today: `replace`, `erase` and `order` are opened by the graph
    // module's operators, and `inline`, `outline` and `eval` are not marked yet -- they arrive as
    // the last row, which is what they were before the record existed. So this reading asks no less
    // than the commit asked before it: the rows above the last are the ones that say no, and only a
    // caller that opens a `NamedEdit` says one.
    tier.effects = editedWith(EditKind::Inline) || editedWith(EditKind::LowLevel);
    tier.order   = editedWith(EditKind::Inline) || editedWith(EditKind::Outline) ||
                   editedWith(EditKind::LowLevel);
    return tier;
}

void GraphDraft::addEditListener(EditListener *listener) {
    if (listener == nullptr ||
        std::ranges::find(editListeners_, listener) != editListeners_.end()) {
        return;
    }
    editListeners_.push_back(listener);
}

void GraphDraft::removeEditListener(EditListener *listener) {
    std::erase(editListeners_, listener);
}

GraphDraft::NamedEdit::NamedEdit(GraphDraft &draft, EditKind kind) : draft_(draft) {
    if (draft_.namedEdits_ == 0) {
        // The outermost edit is the one the table names, and it is the one that records: a named
        // edit written in terms of another is still one row of the table.
        draft_.namedEditKind_ = kind;
        draft_.namedEditRegions_.clear();
    }
    ++draft_.namedEdits_;
}

GraphDraft::NamedEdit::~NamedEdit() {
    --draft_.namedEdits_;
    if (draft_.namedEdits_ != 0) {
        return;
    }
    const EditKind kind = draft_.namedEditKind_;
    std::vector<uint32_t> regions;
    regions.swap(draft_.namedEditRegions_);
    // A named edit whose primitives touched nothing that records a region still is an edit: it is
    // recorded with the empty region list, so that the kind is in the record and the listeners were
    // told, and a draft that used only it owes the tier its row says.
    draft_.recordEdit(kind, regions);
}

} // namespace camel::runtime