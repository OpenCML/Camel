// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Generic optimization passes and the integrated pipeline (algebra lives in algebra.cpp).
 */

#include "generic.h"

#include "rewrite.h"

#include "../inline/engine.h"

#include "camel/core/mm/root_handle.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tags.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/operator_instance.h"
#include "camel/execute/pass/base.h"
#include "camel/execute/pass/warnings.h"
#include "camel/runtime/control.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_types.h"
#include "camel/runtime/draft_writes.h"

#include <deque>
#include <map>
#include <optional>
#include <unordered_set>
#include <queue>
#include <tuple>
#include <vector>

using namespace camel::passes::generic;
using camel::runtime::DraftBrchPayload;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::isWrittenInPlace;
using camel::runtime::kInvalidNodeRef;
namespace type = camel::core::type;

namespace {

// ---------------------------------------------------------------- branch regions

/**
 * For every node, the control regions it lies in (the Arm regions around it, sorted). Two nodes
 * may only be merged when they lie in exactly the same ones, so the survivor runs whenever the
 * replaced node would have.
 */
std::vector<std::vector<uint64_t>> armRegions(const GraphDraft &draft) {
    std::vector<std::vector<uint64_t>> regions(draft.nodeSlotCount());
    for (gc_node_ref_t n = 0; n < regions.size(); ++n) {
        if (!draft.alive(n)) {
            continue;
        }
        for (uint32_t r = draft.nodeRegion(n); r != 0; r = draft.region(r).parent) {
            const auto kind = draft.region(r).kind;
            if (kind == camel::runtime::GCRegionKind::Arm ||
                kind == camel::runtime::GCRegionKind::LoopCond ||
                kind == camel::runtime::GCRegionKind::LoopBody) {
                regions[n].push_back(r);
            }
        }
        std::ranges::sort(regions[n]);
    }
    return regions;
}

// ---------------------------------------------------------------- fold

/// Operator arguments backed by static slots.
class StaticArgsView final : public ArgsView {
  public:
    StaticArgsView(std::vector<slot_t> slots, std::vector<type::Type *> types)
        : slots_(std::move(slots)), types_(std::move(types)) {}
    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    type::TypeCode code(size_t index) const override { return types_[index]->code(); }
    type::Type *type(size_t index) const override { return types_[index]; }

  private:
    std::vector<slot_t> slots_;
    std::vector<type::Type *> types_;
};

/// Whether a fold left no value at all. A lowering applied to the case its tag does not select
/// returns the zero slot (tagged.cpp), and a member the constant does not have reads as one: that
/// is no value of a type that has no null. Materializing it would put a constant where the type
/// promises an object, and a later fold would read the empty slot as that object -- `kind(v)` of
/// a failed `Json!`, in the match arm the tag does not take, read a document at a null pointer.
bool holdsNoValue(slot_t value, type::Type *type) {
    return value == NullSlot && type != nullptr && type::isGCTraced(type->code()) &&
           type::asNullable(type) == nullptr;
}

/// What a constant's own shape costs on top of the bytes of its elements: the object header and
/// the table of extents and strides an array carries, the header of a tuple, a struct or a string.
/// Counted so that a value made of many small pieces is not free of charge.
constexpr size_t kConstantShapeOverheadBytes = 4 * sizeof(slot_t);

/// How deep the walk below follows a value's members. A constant that shares a member with itself
/// (or a value whose contents this estimate does not know) ends here instead of walking forever.
constexpr size_t kConstantEstimateDepth = 8;

/**
 * The bytes a value takes once it is a constant of the graph: its elements (count x element size)
 * plus the shape that describes them (kConstantShapeOverheadBytes). Folding replaces a computation
 * by the value it produces, so this is the compile-time memory that buys the run time; a value past
 * the pass's budget is not worth it and stays the computation that builds it. The estimate stops
 * once it passes `stopAt` -- the caller only asks whether the value fits, never how big it is --
 * and so neither allocates nor walks a large value whole.
 *
 * It counts what the value occupies, not what it costs to build: an array of a hundred million
 * `int` is 800 MB here, as it would be in the graph.
 */
size_t constantBytesOf(
    slot_t value, const type::Type *type, size_t stopAt, size_t depth = 0) {
    if (value == NullSlot || type == nullptr || !type::isGCTraced(type->code())) {
        return sizeof(slot_t);
    }
    const auto *object = camel::core::rtdata::fromSlot<const camel::core::rtdata::Object *>(value);
    if (object == nullptr || stopAt == 0 || depth >= kConstantEstimateDepth) {
        return 0;
    }
    const auto fits = [&](size_t taken) { return taken < stopAt ? stopAt - taken : 0; };
    size_t bytes    = kConstantShapeOverheadBytes;
    switch (type->code()) {
    case type::TypeCode::Array: {
        const auto *array = static_cast<const Array *>(object);
        // The elements, plus the extent and stride table the rank adds to the header.
        bytes += sizeof(Array) + array->rank() * 2 * sizeof(int64_t) +
                 array->byteSize();
        // An element that is a reference is one slot, which the element bytes above already
        // count; the objects the slots point at are counted here.
        if (array->isSlots() && array->elemType() != nullptr &&
            type::isGCTraced(array->elemType()->code())) {
            for (uint64_t i = 0; i < array->numel() && bytes < stopAt; ++i) {
                bytes += constantBytesOf(
                    array->getSlot(i), array->elemType(), fits(bytes), depth + 1);
            }
        }
        break;
    }
    case type::TypeCode::Tuple: {
        const auto *tuple     = static_cast<const Tuple *>(object);
        const auto *tupleType = static_cast<const type::TupleType *>(type);
        bytes += sizeof(Tuple) + tuple->size() * sizeof(slot_t);
        for (size_t i = 0; i < tuple->size() && i < tupleType->size() && bytes < stopAt; ++i) {
            const type::Type *field = tupleType->typeAt(i);
            if (field != nullptr && type::isGCTraced(field->code())) {
                bytes += constantBytesOf(tuple->slotAt(i), field, fits(bytes), depth + 1);
            }
        }
        break;
    }
    case type::TypeCode::Struct: {
        const auto *record     = static_cast<const Struct *>(object);
        const auto *structType = static_cast<const type::StructType *>(type);
        bytes += sizeof(Struct) + record->size() * sizeof(slot_t);
        for (size_t i = 0; i < record->size() && i < structType->size() && bytes < stopAt; ++i) {
            const type::Type *field = structType->typeAt(i);
            if (field != nullptr && type::isGCTraced(field->code())) {
                bytes += constantBytesOf(record->slotAt(i), field, fits(bytes), depth + 1);
            }
        }
        break;
    }
    case type::TypeCode::String: {
        const auto *text = static_cast<const String *>(object);
        bytes += sizeof(String) + text->size() + 1;
        break;
    }
    case type::TypeCode::Complex64:
    case type::TypeCode::Complex128: {
        // Two parts, the wider of the two types: a number is not a container.
        bytes += 2 * sizeof(double);
        break;
    }
    default:
        // A closure, a stream, an enumeration or a module type: this estimate does not know what
        // its contents are, so it counts the header and not the members.
        break;
    }
    return bytes;
}

/**
 * Whether a folded value may be put into the graph as a constant. A value above the pass's
 * `constant_budget` is left as the computation that builds it: not truncated and not refused, so
 * the program means the same and runs the same, only with that computation still in it. A budget
 * of 0 materializes nothing at all, like `growth_budget`'s 0 copying nothing.
 *
 * The question is asked after the operator ran, because its result's size is not known before: a
 * value too large for the budget still costs the one allocation that built it (and, for an
 * operator whose type does not carry the extents, the pass cannot know earlier). What the budget
 * bounds is what the graph keeps, which is what the program would carry and what the export
 * would write; the value itself is dropped here and collected as garbage.
 */
bool fitsConstantBudget(slot_t value, type::Type *type, size_t budget) {
    return constantBytesOf(value, type, budget) < budget;
}

/// Static DATA, seen through GATEs or a JOIN whose arms all forward the same scalar bits.
/// This proves a value, not permission to discard its producer: replaceNode retains ordering.
/// A shared budget bounds gate walks; nested JOINs are not recursively expanded.
std::optional<std::pair<slot_t, type::Type *>>
staticValueOf(const GraphDraft &draft, gc_node_ref_t id) {
    size_t budget     = 128;
    const auto ungate = [&](gc_node_ref_t node) {
        while (budget) {
            --budget;
            const auto *h = draft.header(node);
            if (!h || h->kind != GCNodeKind::Gate || draft.normInputsOf(node).size() != 1 ||
                isWrittenInPlace(draft, node)) {
                return node;
            }
            node = draft.normInputsOf(node).front();
        }
        return kInvalidNodeRef;
    };
    const auto dataValue =
        [&](gc_node_ref_t node) -> std::optional<std::pair<slot_t, type::Type *>> {
        const auto *h = draft.header(node);
        if (!h || h->kind != GCNodeKind::Data || h->dataIndex >= 0) {
            return std::nullopt;
        }
        const auto index = static_cast<size_t>(-h->dataIndex);
        if (index >= draft.staticSlots().size() || isWrittenInPlace(draft, node)) {
            return std::nullopt;
        }
        type::Type *ty = draft.staticSlotTypes()[index];
        return std::pair{draft.staticSlots()[index], ty ? ty : h->dataType};
    };
    id            = ungate(id);
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Join) {
        return dataValue(id);
    }
    if (!h->dataType || !type::isPrimitive(h->dataType->code()) ||
        type::isGCTraced(h->dataType->code()) || isWrittenInPlace(draft, id)) {
        return std::nullopt;
    }
    std::optional<std::pair<slot_t, type::Type *>> common;
    for (auto arm : draft.withInputsOf(id)) {
        const auto value = dataValue(ungate(arm));
        // Slot equality preserves signed zero and NaN payloads; floating-point == would not.
        if (!value || value->second != h->dataType || (common && *common != *value)) {
            return std::nullopt;
        }
        common = value;
    }
    return common;
}

/// True when `id` can be replaced by another node holding its value: it does not anchor the
/// graph, nothing writes it in place, and nothing its value is computed from reads a slot the
/// program writes. A written node is not one value: it is a variable's cell or an object an
/// operator changes in place (a constant's object is shared by every run of the graph), and
/// readers ordered after the writer see what it wrote. The same holds of the nodes a value is
/// computed from -- a slot holding what each write put there is the value of one read, not of the
/// graph -- so folding across such a read gives every reader the value of one of them.
///
/// The value the graph returns is not one of the anchors that pin a node: it is the value of the
/// function's last expression, and when that expression is the one being folded, the function
/// returns what the fold computed. In a function graph the returned value is also the graph's
/// exit and output value (the EXIT marker is drawn from it when the graph is encoded), so those
/// anchors follow it as well (`replaceFoldResult`). Leaving it pinned is what kept a conversion
/// written in return position (the CAST a named result type adds) in the graph forever: every
/// folding round skipped it, and every inlining of that function copied the unfolded conversion.
/// A returned value that waits on something (`sync` bodies end in a gate that carries that
/// ordering) is not folded: it is the tail of the graph's effect order, and a constant replacing
/// it would carry none of it. The wait is read in both kinds the chain of a block is written as
/// (docs/design/effects/edges.cn.md 1.5 and 5.3), the control input and the sync edge. A returned
/// gate that waits carries the chain behind it: the statements of its block run because the graph's
/// result is reached through them (edges.cn.md 2), so folding it to its own input detaches every
/// one of them.
bool isFoldable(const GraphDraft &draft, gc_node_ref_t id) {
    const bool waits = !draft.ctrlInputsOf(id).empty() || !draft.syncInputsOf(id).empty();
    const bool valueAnchor = isReplaceable(draft, id) || (id == draft.returnNode() && !waits);
    return valueAnchor && !isWrittenInPlace(draft, id) && readsNoWrittenSlot(draft, id);
}

/// Replaces `id` by `replacement` (see replaceNode) and makes the replacement the graph's exit,
/// output and returned value wherever `id` was one of them. `replaceNode` re-points the value
/// anchors it knows (output, return), but erasing the node clears the exit anchor, so all three
/// are set again on the replacement.
void replaceFoldResult(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t id, gc_node_ref_t replacement) {
    auto &draft          = transaction.edit();
    const bool wasExit   = draft.exitNode() == id;
    const bool wasOutput = draft.outputNode() == id;
    const bool wasReturn = draft.returnNode() == id;
    const auto kind      = draft.returnKind();
    replaceNode(transaction, id, replacement);
    if (replacement == kInvalidNodeRef) {
        return;
    }
    if (wasExit) {
        draft.setExitNode(replacement);
    }
    if (wasOutput) {
        draft.setOutputNode(replacement);
    }
    if (wasReturn) {
        draft.setReturnNode(replacement, kind);
    }
}

std::optional<StaticArgsView>
staticArgs(const GraphDraft &draft, std::span<const gc_node_ref_t> inputs) {
    std::vector<slot_t> slots;
    std::vector<type::Type *> types;
    for (gc_node_ref_t in : inputs) {
        auto value = staticValueOf(draft, in);
        if (!value || !value->second) {
            return std::nullopt;
        }
        slots.push_back(value->first);
        types.push_back(value->second);
    }
    return StaticArgsView(std::move(slots), std::move(types));
}

// ---------------------------------------------------------------- branch pruning

/// The arm a BRCH takes, when its condition (and, for a match, every case) is a static scalar.
std::optional<size_t> staticArmOf(const GraphDraft &draft, gc_node_ref_t brch) {
    const auto conds = draft.normInputsOf(brch);
    if (conds.size() != 1) {
        return std::nullopt;
    }
    const auto cond = staticValueOf(draft, conds[0]);
    if (!cond || !cond->second || type::isGCTraced(cond->second->code())) {
        return std::nullopt;
    }
    const auto cases = draft.withInputsOf(brch);
    if (cases.empty()) {
        // if/else: arm 0 when true (same selection as the VMs).
        return camel::core::rtdata::fromSlot<bool>(cond->first) ? 0 : 1;
    }
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto value = staticValueOf(draft, cases[i]);
        if (!value || !value->second || type::isGCTraced(value->second->code())) {
            return std::nullopt;
        }
        if (value->first == cond->first) {
            return i;
        }
    }
    return cases.size(); // the default arm
}

/**
 * The nodes of each arm of `brch`: those in the region of the arm (the branches inside the arm
 * included), or nullopt when an arm has no region.
 */
std::optional<std::vector<std::vector<gc_node_ref_t>>>
branchArmNodes(const GraphDraft &draft, gc_node_ref_t brch, size_t armCount) {
    std::vector<uint32_t> armRegion(armCount, 0);
    for (uint32_t r = 1; r < draft.regionCount(); ++r) {
        const auto &region = draft.region(r);
        if (region.kind == camel::runtime::GCRegionKind::Arm && region.owner == brch &&
            region.index < armCount) {
            armRegion[region.index] = r;
        }
    }
    if (std::ranges::find(armRegion, 0u) != armRegion.end()) {
        return std::nullopt;
    }
    std::vector<std::vector<gc_node_ref_t>> arms(armCount);
    for (gc_node_ref_t n = 0; n < draft.nodeSlotCount(); ++n) {
        if (!draft.alive(n)) {
            continue;
        }
        for (uint32_t r = draft.nodeRegion(n); r != 0; r = draft.region(r).parent) {
            const auto &region = draft.region(r);
            if (region.kind == camel::runtime::GCRegionKind::Arm && region.owner == brch &&
                region.index < armCount) {
                arms[region.index].push_back(n);
                break;
            }
        }
    }
    return arms;
}

/**
 * Replaces a branch whose arm is statically known by that arm's code: the other arms are removed,
 * the JOIN's users read the taken arm's result, and the taken arm runs after what the BRCH ran
 * after. Returns false (and changes nothing) for shapes it does not handle.
 */
/// The statements of one block are one chain (docs/design/effects/edges.cn.md 5.3), but they do not
/// always arrive as one: a splice lays the body it moved in inside the arm of a branch, and a prune
/// moves that arm's code back out, so a block collects the code of several bodies with one head and
/// one tail of its own each. The fragments are joined here, in the order they were laid out, skipping
/// a join that would close the chain on itself: layout order is not dependency order, and a fragment
/// that already waits on a later one is left where it is rather than made part of a cycle. Returns
/// the head and the tail of the chain the block's statements make, or two invalid refs when they do
/// not make one.
std::pair<gc_node_ref_t, gc_node_ref_t>
joinBlockFragments(GraphDraft &draft, gc_node_ref_t anchor) {
    const std::pair<gc_node_ref_t, gc_node_ref_t> none{kInvalidNodeRef, kInvalidNodeRef};
    if (anchor == kInvalidNodeRef || !draft.alive(anchor)) {
        return none;
    }
    const auto inBlock = [&](gc_node_ref_t n) {
        return draft.alive(n) && camel::runtime::sameSyncBlock(draft, n, anchor);
    };
    std::vector<gc_node_ref_t> heads;
    for (gc_node_ref_t n = 0; n < draft.nodeSlotCount(); ++n) {
        const camel::runtime::DraftNodeHeader *header = draft.header(n);
        if (header == nullptr || !draft.alive(n) ||
            (header->runtimeFlags & camel::runtime::kGCNodeFlagSyncStatement) == 0 ||
            !camel::runtime::sameSyncBlock(draft, n, anchor)) {
            continue;
        }
        bool waits = false;
        for (gc_node_ref_t pred : draft.syncInputsOf(n)) {
            if (inBlock(pred)) {
                waits = true;
                break;
            }
        }
        if (!waits) {
            heads.push_back(n);
        }
    }
    if (heads.empty()) {
        return none;
    }
    std::vector<gc_node_ref_t> tails;
    bool joinable = true;
    for (gc_node_ref_t head : heads) {
        gc_node_ref_t tail = head;
        for (;;) {
            gc_node_ref_t next = kInvalidNodeRef;
            for (gc_node_ref_t user : draft.syncUsersOf(tail)) {
                if (!inBlock(user)) {
                    continue;
                }
                if (next != kInvalidNodeRef) {
                    joinable = false; // the fragment forks: it has no one tail
                    break;
                }
                next = user;
            }
            if (!joinable || next == kInvalidNodeRef) {
                break;
            }
            tail = next;
        }
        if (!joinable) {
            break;
        }
        tails.push_back(tail);
    }
    if (!joinable || tails.size() != heads.size()) {
        return none;
    }
    // Does `from` wait on `target`, directly or through any edge kind? A join is written only where
    // the answer is no; an exhausted walk answers yes, because not proving it free does not make the
    // join safe.
    const auto waitsOn = [&](gc_node_ref_t from, gc_node_ref_t target) {
        std::vector<gc_node_ref_t> work{from};
        std::unordered_set<gc_node_ref_t> seen;
        size_t budget = size_t{1} << 14;
        while (!work.empty()) {
            const gc_node_ref_t node = work.back();
            work.pop_back();
            if (node == target) {
                return true;
            }
            if (!seen.insert(node).second) {
                continue;
            }
            for (const auto &group :
                 {draft.normInputsOf(node),
                  draft.withInputsOf(node),
                  draft.ctrlInputsOf(node),
                  draft.syncInputsOf(node)}) {
                for (gc_node_ref_t input : group) {
                    if (budget == 0) {
                        return true;
                    }
                    --budget;
                    work.push_back(input);
                }
            }
        }
        return false;
    };
    const auto markStatement = [&](gc_node_ref_t n) {
        const camel::runtime::DraftNodeHeader *header = draft.header(n);
        if (header != nullptr) {
            draft.setNodeRuntimeFlags(
                n,
                static_cast<uint8_t>(header->runtimeFlags |
                                     camel::runtime::kGCNodeFlagSyncStatement));
        }
    };
    bool joined = true;
    for (size_t i = 1; i < heads.size(); ++i) {
        if (waitsOn(tails[i - 1], heads[i])) {
            joined = false;
            continue;
        }
        markStatement(heads[i]);
        markStatement(tails[i - 1]);
        const gc_node_ref_t only[1] = {tails[i - 1]};
        draft.setSyncInputs(heads[i], only);
        camel::runtime::appendControlDependency(draft, tails[i - 1], heads[i]);
    }
    if (!joined) {
        return none;
    }
    return {heads.front(), tails.back()};
}

bool pruneBranch(GraphDraft &draft, gc_node_ref_t brch) {
    const auto arm = staticArmOf(draft, brch);
    if (!arm) {
        return false;
    }
    const gc_node_ref_t join =
        reinterpret_cast<const DraftBrchPayload *>(draft.payloadOf(brch).data())->join;
    const size_t armCount  = draft.armCountOf(brch);
    const auto *joinHeader = draft.header(join);
    if (*arm >= armCount || !joinHeader || joinHeader->kind != GCNodeKind::Join ||
        draft.withInputsOf(join).size() != armCount) {
        return false;
    }
    const gc_node_ref_t result = draft.withInputsOf(join)[*arm];

    auto found = branchArmNodes(draft, brch, armCount);
    if (!found) {
        return false;
    }
    const auto &regions = *found;
    // The arms' code must stay inside the branch: nothing but the JOIN may read it.
    for (const auto &region : regions) {
        for (gc_node_ref_t n : region) {
            for (auto users : {draft.normUsersOf(n),
                               draft.withUsersOf(n),
                               draft.ctrlUsersOf(n),
                               draft.syncUsersOf(n)}) {
                for (gc_node_ref_t u : users) {
                    if (u != join && std::ranges::find(region, u) == region.end()) {
                        return false;
                    }
                }
            }
        }
    }

    std::vector<gc_node_ref_t> brchPreds = draft.waitInputsOf(brch);
    // The branch also ran after what its condition was ordered after: a condition whose value
    // is known may still wait on an effect (the result of a call that prints, seen through a
    // GATE). With the BRCH gone nothing else may reach that effect, and it was silently dropped.
    for (gc_node_ref_t source : orderedSources(draft, brch)) {
        if (source != brch && std::ranges::find(brchPreds, source) == brchPreds.end()) {
            brchPreds.push_back(source);
        }
    }

    // What ran after the BRCH (the taken arm's head, and any other control user outside the
    // removed arms) now runs after the BRCH's predecessors. With none, it waits on nothing; GATEs
    // left waiting on nothing are dissolved below.
    std::vector<bool> dropped(draft.nodeSlotCount(), false);
    for (size_t i = 0; i < regions.size(); ++i) {
        for (gc_node_ref_t n : regions[i]) {
            dropped[n] = i != *arm;
        }
    }
    const auto removed = [&](gc_node_ref_t n) { return static_cast<bool>(dropped[n]); };
    // The taken arm's chain tail: the statement the arm's block value reads -- the sync predecessor
    // of the value the arm gives, when that value is the gate forwarding it -- or, when the block's
    // value carries no chain of its own (a void block, whose value is a constant), the last
    // statement of the arm's chain: the node of the arm that nothing of the arm follows
    // (docs/design/effects/edges.cn.md 5.3: the chain's tail is what the block's value reads).
    //
    // With the branch gone the taken arm's code is code of this block, so the statements written
    // after the branch wait on this node and the arm's code stands in the chain in the branch's
    // place. The chain is what says which statements are reached from the graph's result and run
    // (docs/design/effects/edges.cn.md 2): waiting on the arm's value alone instead steps over the
    // arm's code whenever that value is not a statement of it. `opt_lambda_effect_lost` is the
    // counterexample -- a `null` arm value with a lambda call in the arm, whose effect stopped
    // running -- and it is why both ends are written and not just the one at the value.
    const std::vector<gc_node_ref_t> armNodes(regions[*arm]);
    const auto inArm = [&](gc_node_ref_t n) {
        return std::ranges::find(armNodes, n) != armNodes.end();
    };
    // The statements of the taken arm are the statements of this block once the branch is gone, and a
    // block's statements are one chain (docs/design/effects/edges.cn.md 5.3). They do not always
    // arrive as one, so the fragments the arm brings are joined before the branch's place is handed
    // over: only one of them would take the place, and the statements of the others would be reached
    // by nothing and would stop running (2).
    gc_node_ref_t armBlockNode = kInvalidNodeRef;
    for (uint32_t r = 1; armBlockNode == kInvalidNodeRef && r < draft.regionCount(); ++r) {
        const auto &region = draft.region(r);
        if (region.kind != camel::runtime::GCRegionKind::Arm || region.owner != brch ||
            region.index != *arm) {
            continue;
        }
        for (gc_node_ref_t n = 0; n < draft.nodeSlotCount(); ++n) {
            if (draft.alive(n) && inArm(n) && draft.nodeRegion(n) == r) {
                armBlockNode = n;
                break;
            }
        }
    }
    const auto markStatement = [&](gc_node_ref_t n) {
        const camel::runtime::DraftNodeHeader *header = draft.header(n);
        if (header != nullptr) {
            draft.setNodeRuntimeFlags(
                n,
                static_cast<uint8_t>(header->runtimeFlags |
                                     camel::runtime::kGCNodeFlagSyncStatement));
        }
    };
    const auto chainTo = [&](gc_node_ref_t user, gc_node_ref_t pred) {
        markStatement(user);
        markStatement(pred);
        const gc_node_ref_t only[1] = {pred};
        draft.setSyncInputs(user, only);
        camel::runtime::appendControlDependency(draft, pred, user);
    };
    gc_node_ref_t armChainHead = kInvalidNodeRef;
    gc_node_ref_t armChainTail = kInvalidNodeRef;
    if (armBlockNode != kInvalidNodeRef) {
        const auto chain  = joinBlockFragments(draft, armBlockNode);
        armChainHead      = chain.first;
        armChainTail      = chain.second;
    }
    gc_node_ref_t armTail = kInvalidNodeRef;
    for (gc_node_ref_t pred : draft.syncInputsOf(result)) {
        if (inArm(pred)) {
            armTail = pred;
        }
    }
    if (armTail == kInvalidNodeRef) {
        for (gc_node_ref_t n : armNodes) {
            if (!carriesOrdering(draft, n, true)) {
                continue;
            }
            bool followed = false;
            for (gc_node_ref_t user : draft.syncUsersOf(n)) {
                if (inArm(user)) {
                    followed = true;
                    break;
                }
            }
            if (!followed) {
                armTail = n;
                break;
            }
        }
    }
    // The chain the arm's fragments were joined into is what the branch's place is handed to: its
    // tail is where the statements written after the branch follow, and its head is entered from
    // where the branch stood.
    const std::vector<gc_node_ref_t> joinChainPreds(
        draft.syncInputsOf(join).begin(),
        draft.syncInputsOf(join).end());
    if (armChainTail != kInvalidNodeRef) {
        armTail = armChainTail;
    }
    const auto &preds = brchPreds;
    const std::vector<gc_node_ref_t> brchCtrlUsers = draft.waitUsersOf(brch);
    for (gc_node_ref_t user : brchCtrlUsers) {
        if (user == join || removed(user)) {
            continue;
        }
        draft.unlinkInput(camel::runtime::DraftEdgeKind::Ctrl, user, brch);
        for (gc_node_ref_t pred : preds) {
            camel::runtime::appendControlDependency(draft, pred, user);
        }
    }

    // The JOIN's value users read the taken result. Its control users ran after the whole
    // branch: they now wait for the result, for what the JOIN itself waited on, and for what the
    // BRCH waited on (the result may come from outside the arm, so waiting on it alone could cut
    // them off from earlier effects).
    const std::vector<gc_node_ref_t> joinCtrlUsers = draft.waitUsersOf(join);
    std::vector<gc_node_ref_t> after{result};
    for (gc_node_ref_t pred : draft.waitInputsOf(join)) {
        after.push_back(pred);
    }
    after.insert(after.end(), brchPreds.begin(), brchPreds.end());
    // Its value users ran after the whole branch too. When the result comes from outside the
    // taken arm (a constant, an input), reading it no longer orders them after what the branch
    // waited on, so they wait on it explicitly (as replaceNode does for a replaced value).
    std::vector<gc_node_ref_t> joinValueUsers;
    for (auto group : {draft.normUsersOf(join), draft.withUsersOf(join)}) {
        for (gc_node_ref_t u : group) {
            if (!dropped[u] && std::ranges::find(joinValueUsers, u) == joinValueUsers.end()) {
                joinValueUsers.push_back(u);
            }
        }
    }
    const bool resultInArm = std::ranges::find(regions[*arm], result) != regions[*arm].end();
    draft.replaceAllValueUses(join, result);
    if (!resultInArm) {
        for (gc_node_ref_t user : joinValueUsers) {
            for (gc_node_ref_t pred : after) {
                if (pred != result && !dropped[pred]) {
                    camel::runtime::appendControlDependency(draft, pred, user);
                }
            }
        }
    }
    for (gc_node_ref_t user : joinCtrlUsers) {
        draft.unlinkInput(camel::runtime::DraftEdgeKind::Ctrl, user, join);
        for (gc_node_ref_t pred : after) {
            if (!dropped[pred]) {
                camel::runtime::appendControlDependency(draft, pred, user);
            }
        }
    }
    if (draft.exitNode() == join) {
        draft.setExitNode(result);
    }
    if (draft.outputNode() == join) {
        draft.setOutputNode(result);
    }
    if (draft.returnNode() == join) {
        draft.setReturnNode(result, draft.returnKind());
    }

    // 5.7: the branch's place in the block's chain is handed to the taken arm's chain tail --
    // the arm's code is the block's code once the branch is dissolved (eraseNode re-parents its
    // regions) -- and the arm's chain is entered from where the branch stood. Both ends are written:
    // waiting on the arm's value alone steps over the arm's code whenever that value is not a
    // statement of it, which is what the counterexample `opt_lambda_effect_lost` is (a `null` arm
    // value with a lambda call in the arm, whose effect stopped running). The call runs before the
    // erases so that eraseNode's own chain closing finds no successor left to close over.
    if (armTail != kInvalidNodeRef && draft.alive(armTail) && draft.alive(join)) {
        draft.handChainPlaceTo(join, armTail);
    }
    // The joined chain's head stands where the branch stood: `handChainPlaceTo` gave the place to the
    // chain's tail, which already waits on the fragment before it, so the entry is written here. The
    // predecessor a head arrives with can be the branch itself -- the builder enters an arm's chain
    // from its BRCH -- and the BRCH is one of the nodes erased below, so that place is not one the
    // head keeps: the branch's own place, the one its tail was handed, takes that slot instead.
    const auto erasedByBranch = [&](gc_node_ref_t n) { return n == brch || n == join || removed(n); };
    if (armChainHead != kInvalidNodeRef && draft.alive(armChainHead)) {
        bool entered = false;
        for (gc_node_ref_t pred : draft.syncInputsOf(armChainHead)) {
            entered = entered || !erasedByBranch(pred);
        }
        if (!entered) {
            for (gc_node_ref_t pred : joinChainPreds) {
                if (pred != armChainHead && pred != join && draft.alive(pred) &&
                    !removed(pred)) {
                    chainTo(armChainHead, pred);
                    break;
                }
            }
        }
    }

    // What the arms that go away read from outside themselves. A branch is used as a whole: it uses
    // every value its arms read from outside (design principles 3.1), so an effect written outside
    // the branch runs when the branch runs, whichever arm is taken (3.3). Dropping the arm must not
    // drop the effect with it (9.7); the nodes are read before the arms are erased, because the
    // reads are the only thing that names them.
    std::vector<gc_node_ref_t> outsideReads;
    {
        std::vector<gc_node_ref_t> work;
        for (size_t i = 0; i < regions.size(); ++i) {
            if (i == *arm) {
                continue;
            }
            for (gc_node_ref_t n : regions[i]) {
                for (auto group : {draft.normInputsOf(n),
                                   draft.withInputsOf(n),
                                   draft.ctrlInputsOf(n),
                                   draft.syncInputsOf(n)}) {
                    work.insert(work.end(), group.begin(), group.end());
                }
            }
        }
        // An arm's code is what its value reads, and an arm whose code is one value read from
        // outside (an arm written as a name: `if c then a else 0`) has no node of its own in the
        // region at all: the JOIN reads it through the with-port of that arm.
        const auto joinValues = draft.withInputsOf(join);
        for (size_t i = 0; i < regions.size() && i < joinValues.size(); ++i) {
            if (i != *arm) {
                work.push_back(joinValues[i]);
            }
        }
        std::vector<bool> queued(draft.nodeSlotCount(), false);
        while (!work.empty()) {
            const gc_node_ref_t n = work.back();
            work.pop_back();
            if (n >= queued.size() || n == brch || n == join || !draft.alive(n) ||
                queued[n]) {
                continue;
            }
            queued[n] = true;
            // What an arm reads is read through the arm's own code as well: the code goes away and
            // what it read from outside is what has to keep running.
            if (removed(n)) {
                for (auto group : {draft.normInputsOf(n),
                                   draft.withInputsOf(n),
                                   draft.ctrlInputsOf(n),
                                   draft.syncInputsOf(n)}) {
                    work.insert(work.end(), group.begin(), group.end());
                }
                continue;
            }
            outsideReads.push_back(n);
        }
    }

    draft.eraseNode(join);
    draft.eraseNode(brch);
    for (gc_node_ref_t n = 0; n < draft.nodeSlotCount(); ++n) {
        if (dropped[n] && draft.header(n)) {
            draft.eraseNode(n);
        }
    }
    // An effect among those reads that only the dropped arm used runs where the branch stood, before
    // the code that took its place: the branch used it, so it happens whether or not that arm is
    // taken, and it is the arm's code that runs when the arm is the taken one. The wait is written
    // on the first thing the surviving code runs -- the taken arm's chain head, or the node that
    // stands at the branch's place, or the effect the code after the branch reaches first.
    {
        std::vector<gc_node_ref_t> lost;
        for (gc_node_ref_t read : outsideReads) {
            if (!draft.alive(read)) {
                continue;
            }
            bool stillRead = false;
            for (auto group : {draft.normUsersOf(read),
                               draft.withUsersOf(read),
                               draft.ctrlUsersOf(read),
                               draft.syncUsersOf(read)}) {
                stillRead = stillRead || !group.empty();
            }
            if (stillRead) {
                continue;
            }
            if (camel::runtime::carriesOrdering(draft, read, true)) {
                lost.push_back(read);
                continue;
            }
            for (gc_node_ref_t source : orderedSources(draft, read)) {
                if (std::ranges::find(lost, source) == lost.end()) {
                    lost.push_back(source);
                }
            }
        }
        std::vector<gc_node_ref_t> targets;
        const auto addTarget = [&](gc_node_ref_t n) {
            if (!draft.alive(n) || std::ranges::find(lost, n) != lost.end()) {
                return;
            }
            if (camel::runtime::carriesOrdering(draft, n, true)) {
                if (std::ranges::find(targets, n) == targets.end()) {
                    targets.push_back(n);
                }
                return;
            }
            for (gc_node_ref_t source : orderedSources(draft, n)) {
                if (source != n && std::ranges::find(lost, source) == lost.end() &&
                    std::ranges::find(targets, source) == targets.end()) {
                    targets.push_back(source);
                }
            }
        };
        if (armChainHead != kInvalidNodeRef) {
            addTarget(armChainHead);
        }
        addTarget(result);
        for (gc_node_ref_t user : joinValueUsers) {
            addTarget(user);
        }
        if (targets.empty()) {
            // Nothing after the branch carries an order (a branch folded to a constant nothing
            // reads): the wait goes on what reads the branch's place, so the effect still runs
            // where the branch stood.
            for (gc_node_ref_t user : joinValueUsers) {
                if (draft.alive(user) && std::ranges::find(lost, user) == lost.end() &&
                    std::ranges::find(targets, user) == targets.end()) {
                    targets.push_back(user);
                }
            }
            const auto *resultHeader =
                result == kInvalidNodeRef ? nullptr : draft.header(result);
            if (targets.empty() && resultHeader != nullptr &&
                resultHeader->kind != GCNodeKind::Data &&
                std::ranges::find(lost, result) == lost.end()) {
                targets.push_back(result);
            }
        }
        for (gc_node_ref_t source : lost) {
            for (gc_node_ref_t target : targets) {
                // The wait is written only between two nodes that are neither ahead of the other.
                // A place that reads the effect already runs after it -- it is the effect's own
                // argument, a gate the effect passes on, or the code the effect calls -- so there
                // is nothing to order there, and writing the edge would close a cycle of inputs.
                if (isComputedAfter(draft, target, source) ||
                    isComputedAfter(draft, source, target)) {
                    continue;
                }
                camel::runtime::appendControlDependency(draft, source, target);
            }
        }
    }
    // Gates that waited only on the branch now wait on nothing; commit dissolves them.
    //
    // What is not done here: joining the block's fragments a second time, now that the arm's code is
    // code of this block. Reading the fragments of a block the arm was just merged into takes the
    // statements of a `var` that was captured and written in the same block for fragments of one
    // chain, and joining them reorders writes the capture rules keep apart (`var_capture`,
    // `captured_write_order`). The arm's fragments are joined above, where the block is still the
    // arm's own and the statements are the arm's.
    return true;
}

/// Replaces an element access on a constant tuple or struct by the element's value.
bool foldStaticAccess(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t id, size_t constantBudget) {
    auto &draft       = transaction.edit();
    const auto sources = draft.normInputsOf(id);
    if (sources.size() != 1) {
        return false;
    }
    const auto source  = staticValueOf(draft, sources[0]);
    type::Type *result = draft.header(id)->dataType;
    if (!source || !source->second || !result || source->first == NullSlot) {
        return false;
    }
    const auto *body =
        reinterpret_cast<const camel::runtime::GCAccsBody *>(draft.payloadOf(id).data());
    slot_t value = NullSlot;
    if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex &&
        source->second->code() == type::TypeCode::Tuple) {
        auto *tuple = camel::core::rtdata::fromSlot<::Tuple *>(source->first);
        if (body->value >= tuple->size()) {
            return false;
        }
        value = tuple->get<slot_t>(body->value);
    } else if (
        body->accsKind == camel::runtime::GCAccsKind::StructKey &&
        source->second->code() == type::TypeCode::Struct) {
        auto *object = camel::core::rtdata::fromSlot<::Struct *>(source->first);
        value        = object->get<slot_t>(std::string(body->key()), source->second);
    } else {
        return false;
    }
    if (holdsNoValue(value, result)) {
        return false;
    }
    if (!fitsConstantBudget(value, result, constantBudget)) {
        return false; // the element is a value of its own, and one too large to hold
    }
    // The element is reachable from the constant it was read from, which the graph keeps.
    replaceFoldResult(
        transaction, id, draft.materializeStaticValue(value, result));
    return true;
}

/// Replaces a FILL whose template and values are all constants by the object it builds.
bool foldStaticFill(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t id,
    std::deque<camel::core::mm::RootHandle> &roots, size_t constantBudget) {
    auto &draft = transaction.edit();
    const auto sources = draft.normInputsOf(id);
    type::Type *result = draft.header(id)->dataType;
    if (sources.size() != 1 || !result || !type::isGCTraced(result->code())) {
        return false;
    }
    const auto *body =
        reinterpret_cast<const camel::runtime::GCFillBody *>(draft.payloadOf(id).data());
    if (body->fillKind == camel::runtime::GCFillKind::FunctionClosure) {
        return false; // closures stay calls std::opt can devirtualize
    }
    const auto source = staticValueOf(draft, sources[0]);
    if (!source || source->first == NullSlot) {
        return false;
    }
    std::vector<slot_t> slots;
    for (gc_node_ref_t in : draft.withInputsOf(id)) {
        const auto value = staticValueOf(draft, in);
        if (!value) {
            return false;
        }
        slots.push_back(value->first);
    }
    // The template carries the shape of the object the fill builds, and its own type is the one
    // that describes that shape, so its size is the size the constant would have. One above the
    // budget is not made at all: folding buys run time with compile-time memory, which a value
    // this large does not repay.
    if (!fitsConstantBudget(source->first, source->second ? source->second : result,
                            constantBudget)) {
        return false;
    }
    auto *object = camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(source->first)
                       ->clone(camel::core::mm::autoSpace(), result, false);
    // Keep the object alive until commit moves it into the graph's static area.
    roots.emplace_back(camel::core::mm::autoSpace(), object, result, "std::opt::fold");
    camel::execute::writeRuntimeFillSlots(object, result, body, slots);
    replaceFoldResult(
        transaction, id,
        draft.materializeStaticValue(camel::core::rtdata::toSlot(object), result));
    return true;
}

/**
 * Replaces a conversion (`e as T`, a CAST node) of a constant by the converted constant: a
 * constant under `as` is still a constant. The conversion is the one the schedulers run
 * (Type::castSlotFrom of the target type), under the rule the graph builder admits a CAST by:
 * a Safe conversion between primitive types, or a conversion into a module type, which checks
 * the value (a tensor's element type and shape). A checked conversion that fails is left in the
 * graph, to fail with its own located error when the program runs.
 */
bool foldStaticCast(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t id,
    std::deque<camel::core::mm::RootHandle> &roots, size_t constantBudget) {
    auto &draft = transaction.edit();
    const auto sources = draft.normInputsOf(id);
    type::Type *target = draft.header(id)->dataType;
    if (sources.size() != 1 || !target || !draft.withInputsOf(id).empty()) {
        return false;
    }
    const auto source = staticValueOf(draft, sources[0]);
    if (!source || !source->second) {
        return false;
    }
    // A conversion of a value above the budget is not made: the conversion of a large array copies
    // it, and the copy is a constant as large as the source.
    if (!fitsConstantBudget(source->first, source->second, constantBudget)) {
        return false;
    }
    const type::CastSafety safety = target->castSafetyFrom(source->second);
    // A module type checks a value of its own kind (the constant's type says it is one).
    const bool checkedByTarget = target->isOtherType() && safety != type::CastSafety::Forbidden &&
                                 source->second->code() == target->code() &&
                                 source->first != NullSlot;
    const bool safePrimitive   = safety == type::CastSafety::Safe &&
                                 type::isPrimitive(target->code()) &&
                                 type::isPrimitive(source->second->code());
    // An unnamed struct literal entering the named type it is written at: the conversion names the
    // type the value is used as, and moves nothing (design principles 5.1, 5.2).
    const bool safeStruct = safety == type::CastSafety::Safe &&
                            target->code() == type::TypeCode::Struct &&
                            source->second->code() == type::TypeCode::Struct;
    // A tensor is an array (`f32[2, 3]`), and its conversion to another array type is the same
    // kind of pure retyping: a Safe conversion checks the element type and the shape and moves
    // nothing, so a constant under it is still that constant. An Unsafe one is a conversion the
    // program means to be checked (a shape that does not fit) and is left in the graph, as the
    // module-type conversion above is.
    const bool safeArray = safety == type::CastSafety::Safe &&
                           target->code() == type::TypeCode::Array &&
                           source->second->code() == type::TypeCode::Array;
    if (!checkedByTarget && !safePrimitive && !safeStruct && !safeArray) {
        return false;
    }
    slot_t result = NullSlot;
    try {
        result = target->castSlotFrom(source->first, source->second);
    } catch (...) {
        return false; // the conversion fails: left to be reported when the program runs
    }
    if (type::isGCTraced(target->code()) && result != NullSlot && result != source->first &&
        !fitsConstantBudget(result, target, constantBudget)) {
        return false; // the conversion succeeded but its value is too large to hold
    }
    if (type::isGCTraced(target->code()) && result != NullSlot && result != source->first) {
        // Keep a value the conversion made alive until commit moves it into the static area.
        roots.emplace_back(
            camel::core::mm::autoSpace(),
            camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(result),
            target,
            "std::opt::fold");
    }
    const gc_node_ref_t data = draft.materializeStaticValue(result, target);
    replaceFoldResult(
        transaction, id, data);
    return true;
}

/// Replaces a pure operator by the constant its argument types fix, through the operator's
/// registered type folder. Returns false when there is none or the types do not fix the result.
bool foldFromTypes(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t id,
    std::deque<camel::core::mm::RootHandle> &roots, size_t constantBudget) {
    auto &draft = transaction.edit();
    const auto *folder     = camel::core::OperatorTable::instance().typeFolder(operIdOf(draft, id));
    type::Type *resultType = draft.header(id)->dataType;
    // The arguments as the operator's kernel reads them (with-arguments after the ordinary ones
    // when it takes them so); an operator with other with-arguments is not folded.
    const camel::runtime::KernelArguments arguments = camel::runtime::kernelArgumentsOf(draft, id);
    if (!folder || !resultType || !arguments.with.empty()) {
        return false;
    }
    std::vector<type::Type *> types;
    std::vector<type::StaticArg> statics;
    for (gc_node_ref_t in : arguments.norm) {
        const auto *h = draft.header(in);
        if (!h || !h->dataType) {
            return false;
        }
        types.push_back(h->dataType);
        const auto value = staticValueOf(draft, in);
        statics.push_back(value ? std::optional<slot_t>(value->first) : std::nullopt);
    }
    const auto result = (*folder)(types, statics, camel::core::mm::autoSpace());
    if (!result || holdsNoValue(*result, resultType)) {
        return false;
    }
    if (!fitsConstantBudget(*result, resultType, constantBudget)) {
        return false; // the value is above the budget: the computation that builds it stays
    }
    if (type::isGCTraced(resultType->code()) && *result != NullSlot) {
        // Keep the value alive until commit moves it into the graph's static area.
        roots.emplace_back(
            camel::core::mm::autoSpace(),
            camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(*result),
            resultType,
            "std::opt::fold");
    }
    replaceFoldResult(
        transaction, id, draft.materializeStaticValue(*result, resultType));
    return true;
}

size_t foldDraft(
    camel::runtime::GraphTransaction &transaction, camel::core::context::Context &ctx,
    std::deque<camel::core::mm::RootHandle> &roots, size_t constantBudget) {
    auto &draft  = transaction.edit();
    size_t folded = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            // A branch whose condition is static is replaced by the taken arm.
            if (const auto *h = draft.header(id); h && h->kind == GCNodeKind::Brch) {
                if (pruneBranch(draft, id)) {
                    ++folded;
                    changed = true;
                }
                continue;
            }
            // An element projected out of a tuple built in this graph is the value filled there;
            // an element of a constant tuple or struct is a constant.
            if (const auto *h = draft.header(id);
                h && h->kind == GCNodeKind::Accs && isFoldable(draft, id)) {
                const gc_node_ref_t value = camel::runtime::resolveTupleProjection(draft, id);
                if (value != id) {
                    replaceFoldResult(
        transaction, id, value);
                    ++folded;
                    changed = true;
                } else if (foldStaticAccess(transaction, id, constantBudget)) {
                    ++folded;
                    changed = true;
                }
                continue;
            }
            // A tuple, struct or array built from constants is a constant.
            if (const auto *h = draft.header(id);
                h && h->kind == GCNodeKind::Fill && isFoldable(draft, id)) {
                if (foldStaticFill(transaction, id, roots, constantBudget)) {
                    ++folded;
                    changed = true;
                }
                continue;
            }
            // A conversion of a constant is a constant, when the conversion succeeds.
            if (const auto *h = draft.header(id);
                h && h->kind == GCNodeKind::Cast && isFoldable(draft, id)) {
                if (foldStaticCast(transaction, id, roots, constantBudget)) {
                    ++folded;
                    changed = true;
                }
                continue;
            }
            if (!isPureOper(transaction, id)) {
                continue;
            }
            // A node that cannot be replaced (the head of a branch arm) is still evaluated, to
            // tell the user of a constant that faults where it is written.
            const bool foldable = isFoldable(draft, id);
            auto with           = staticArgs(draft, draft.withInputsOf(id));
            auto norm           = staticArgs(draft, draft.normInputsOf(id));
            if (!with || !norm) {
                if (!foldable) {
                    continue;
                }
                // Not all arguments are constants, but their types may fix the result (e.g. the
                // shape of a tensor whose type carries it).
                if (foldFromTypes(transaction, id, roots, constantBudget)) {
                    ++folded;
                    changed = true;
                }
                continue;
            }
            const auto *body = reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data());
            operator_t op    = body->op;
            if (!op) {
                auto found = ctx.execMgr().find(std::string(body->uri()));
                if (!found) {
                    continue;
                }
                op = *found;
            }
            type::Type *resultType = draft.header(id)->dataType;
            with->setResultType(resultType);
            norm->setResultType(resultType);
            slot_t result = NullSlot;
            try {
                result = (*op)(*with, *norm, ctx);
            } catch (const camel::core::error::RuntimeFault &fault) {
                // A constant expression that can only fault (`1 / 0`) is not folded: it is
                // left as written, to fault with its position if the run reaches it, so the
                // program behaves the same with and without the pass. The user is told once,
                // where the expression is written (design principles 3.9).
                // `fail` is the exception: it states a fault on purpose, and the warning is for
                // the accident.
                if (body->uri() != ":op/fail") {
                    raisePassWarning(
                        ctx,
                        camel::core::error::RuntimeDiag::ConstantExpressionFaults,
                        draft.nodeOrigin(id),
                        fault.what());
                }
                continue;
            } catch (...) {
                continue; // leave the failure to be reported when the program runs
            }
            if (!foldable) {
                continue;
            }
            if (holdsNoValue(result, resultType)) {
                // The lowering returned the zero slot for the case its tag does not select: no
                // constant goes where the type promises an object (see holdsNoValue).
                continue;
            }
            if (!fitsConstantBudget(result, resultType, constantBudget)) {
                // The value is above what the pass may materialize: the computation that builds
                // it stays in the graph (design principles 9.6). It is not truncated and not
                // refused -- the program means the same, only less folded.
                continue;
            }
            if (resultType && type::isGCTraced(resultType->code()) && result != NullSlot) {
                // Keep the value alive until commit moves it into the graph's static area.
                roots.emplace_back(
                    camel::core::mm::autoSpace(),
                    camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(result),
                    resultType,
                    "std::opt::fold");
            }
            const gc_node_ref_t value = draft.materializeStaticValue(result, resultType);
            replaceFoldResult(
        transaction, id, value);
            ++folded;
            changed = true;
        }
        // Constants can sharpen the types downstream (a reshape to a now constant shape), which
        // the type folders turn into further constants.
        if (changed) {
            (void)camel::runtime::reinferDraftTypes(draft);
        }
    }
    return folded;
}

// ---------------------------------------------------------------- cse

size_t cseDraft(camel::runtime::GraphTransaction &transaction) {
    auto &draft = transaction.edit();
    const auto regions = armRegions(draft);
    // The numeric permissions are part of the key: two otherwise identical operations granted
    // different permissions (design principles 7.5) may compute different bits, and merging
    // them would give one of them permissions it was not granted, or take some away. They are
    // not merged (docs/design/op-semantics 6.4).
    using Key = std::tuple<
        std::string,
        type::Type *,
        std::vector<gc_node_ref_t>,
        std::vector<gc_node_ref_t>,
        std::vector<uint64_t>,
        uint8_t>;
    // The node attributes are not part of the key: each attribute's key says whether two
    // nodes that differ in it may become one and what the one keeps (attr::merge). The nodes
    // of one key, each a candidate a later node may be merged into.
    std::map<Key, std::vector<gc_node_ref_t>> seen;
    size_t merged = 0;

    // Equal scalar constants are one value: merge their DATA nodes first, so expressions over
    // separately written literals (`n * 7` twice) become identical. GC-traced constants keep
    // their identity (objects may be compared by reference). staticValueOf() leaves out
    // constants that are written in place.
    std::map<std::tuple<type::Type *, slot_t, std::vector<uint64_t>>, gc_node_ref_t> constants;
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        // A known JOIN value does not make the JOIN replaceable: it still closes its BRCH
        // and carries the condition and selected arm's effects. Consumers may fold instead.
        if (const auto *h = draft.header(id); h && h->kind == GCNodeKind::Join) {
            continue;
        }
        const auto value = staticValueOf(draft, id);
        if (!value || !value->second || type::isGCTraced(value->second->code())) {
            continue;
        }
        auto [it, inserted] = constants.try_emplace({value->second, value->first, regions[id]}, id);
        if (!inserted && isReplaceable(draft, id) && !reaches(draft, id, it->second)) {
            replaceNode(transaction, id, it->second);
            ++merged;
        }
    }

    // Two identical expressions over an object that is changed in place are not one value: a
    // write may lie between them (`len(a)`, `a = a->append<4>`, `len(a)`). The object may be
    // passed to the writer under this node, or be the one a variable holds (`:var/get`), which
    // every other reference to the variable reaches. Neither expression is merged, and a
    // result that is itself written keeps its own slot.
    const auto readsVariable = [&](gc_node_ref_t id) {
        for (auto inputs : {draft.normInputsOf(id), draft.withInputsOf(id)}) {
            for (gc_node_ref_t in : inputs) {
                const auto *h = draft.header(in);
                // A read of a cell is an operator that declares an access to a variable.
                if (isWrittenInPlace(draft, in) ||
                    (h && h->kind == GCNodeKind::Oper &&
                     camel::core::OperatorTable::instance().touchesClass(
                         operIdOf(draft, in),
                         camel::core::sem::ResourceClass::Variable))) {
                    return true;
                }
            }
        }
        return false;
    };
    // Two expressions whose inputs differ only in the gates they pass through compute one value: a
    // gate forwards the value it reads and changes nothing else (camel::runtime::ungated,
    // camel/runtime/draft.h; docs/design/effects/edges.cn.md 4.6 and 5.3). The gate keeps its place
    // and its wait either way, so this merges the computation and leaves the order it carried.
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!isPureOper(transaction, id) || isWrittenInPlace(draft, id) || readsVariable(id)) {
            continue;
        }
        const auto norm = draft.normInputsOf(id);
        const auto with = draft.withInputsOf(id);
        std::vector<gc_node_ref_t> normKey;
        std::vector<gc_node_ref_t> withKey;
        normKey.reserve(norm.size());
        withKey.reserve(with.size());
        for (gc_node_ref_t in : norm) {
            normKey.push_back(camel::runtime::ungated(draft, in));
        }
        for (gc_node_ref_t in : with) {
            withKey.push_back(camel::runtime::ungated(draft, in));
        }
        Key key{
            std::string(operUriOf(transaction, id)),
            draft.header(id)->dataType,
            std::move(normKey),
            std::move(withKey),
            regions[id],
            camel::core::tag::permissionsOf(draft, id)};
        std::vector<gc_node_ref_t> &candidates = seen[std::move(key)];
        bool replaced                          = false;
        if (isReplaceable(draft, id)) {
            for (gc_node_ref_t kept : candidates) {
                if (reaches(draft, id, kept)) {
                    continue;
                }
                const auto attrs =
                    camel::core::attr::merge(draft.nodeAttrs(kept), draft.nodeAttrs(id));
                if (!attrs.has_value()) {
                    continue;
                }
                draft.setNodeAttrs(kept, *attrs);
                replaceNode(transaction, id, kept);
                ++merged;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            candidates.push_back(id);
        }
    }
    return merged;
}

// ---------------------------------------------------------------- dce

/**
 * For a GATE with no users at all, check that erasing it loses no ordering: every node it
 * waits on is a pure value, or has another user through which it still reaches the exit.
 * Gates with control users do not need this check: replaceNode transfers their dependencies
 * to those users, including an effect whose only current successor is the gate. A wait is read in
 * either kind: a statement of a `sync` block is waited on through the sync edge, and one of those
 * would be reached by nothing once the gate goes unless it has a user of its own.
 */
bool gateInputsSurvive(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t gate) {
    const auto &draft = transaction.graph();
    for (auto inputs : {draft.normInputsOf(gate),
                        draft.withInputsOf(gate),
                        draft.ctrlInputsOf(gate),
                        draft.syncInputsOf(gate)}) {
        for (gc_node_ref_t in : inputs) {
            if (isValueOnly(draft, in)) {
                continue;
            }
            size_t users = 0;
            for (auto group : {draft.normUsersOf(in),
                               draft.withUsersOf(in),
                               draft.ctrlUsersOf(in),
                               draft.syncUsersOf(in)}) {
                for (gc_node_ref_t user : group) {
                    users += user != gate ? 1 : 0;
                }
            }
            if (users == 0) {
                return false;
            }
        }
    }
    return true;
}

/// With valuesOnly, only the nodes that are no more than their value: constants and pure
/// operations (also casts, copies, accesses and fills) that no node reads; gates and syncs stay.
size_t dceDraft(camel::runtime::GraphTransaction &transaction, bool valuesOnly = false) {
    auto &draft = transaction.edit();
    size_t removed        = 0;
    size_t gateEdgeBudget = std::min<size_t>(8192, draft.nodeSlotCount() * 8);
    for (bool changed = true; changed;) {
        changed = false;
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (!valuesOnly && bypassValueGate(transaction, id, gateEdgeBudget)) {
                ++removed;
                changed = true;
                continue;
            }
            // A GATE only forwards its value once its control inputs ran. When no node reads the
            // value it is a wait that also depends on a value, and it goes the same way: the
            // nodes that wait on it wait on what it waited on instead (its control inputs and
            // the ordered nodes its value came from). Keeping such a GATE because other nodes
            // wait on it kept its value alive for nothing: a closure that is only called
            // through a branch stayed read by the dead GATE, and the export could not split
            // that call across the branch. A GATE that nothing uses at all stays when it is the
            // only way something it waits on reaches the exit.
            const auto *h          = draft.header(id);
            const bool isDeadGate =
                !valuesOnly && h && h->kind == GCNodeKind::Gate &&
                (!draft.waitUsersOf(id).empty() || gateInputsSurvive(transaction, id));
            if ((isDeadWhenUnread(draft, id) || isDeadGate) && draft.normUsersOf(id).empty() &&
                draft.withUsersOf(id).empty() && isReplaceable(draft, id)) {
                replaceNode(transaction, id, kInvalidNodeRef);
                ++removed;
                changed = true;
            }
        }
    }
    return removed;
}

} // namespace

const camel::runtime::Program *
ConstantFoldPass::apply(const camel::runtime::Program *program, std::ostream &) {
    std::deque<camel::core::mm::RootHandle> roots;
    return rewriteReachableGraphs(
        context_,
        program,
        "std::opt::fold",
        [&](camel::runtime::GraphTransaction &transaction) {
            auto &draft = transaction.edit();
            return foldDraft(transaction, *context_, roots, constantBudgetBytes_);
        },
        nullptr,
        nullptr,
        mayFold);
}

const camel::runtime::Program *
CommonSubexpressionPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return rewriteReachableGraphs(
        context_,
        program,
        "std::opt::cse",
        cseDraft,
        nullptr,
        nullptr,
        mayMerge);
}

const camel::runtime::Program *
DeadCodePass::apply(const camel::runtime::Program *program, std::ostream &) {
    return rewriteReachableGraphs(
        context_,
        program,
        "std::opt::dce",
        [&](camel::runtime::GraphTransaction &transaction) {
            auto &draft = transaction.edit();
            return dceDraft(transaction, valuesOnly_);
        },
        nullptr,
        nullptr,
        mayRemoveDead);
}

const camel::runtime::Program *
OrderReductionPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return rewriteReachableGraphs(
        context_,
        program,
        "std::opt::order",
        [](camel::runtime::GraphTransaction &transaction) {
            auto &draft = transaction.edit();
            return camel::runtime::reduceOrderDependencies(draft) +
                   camel::runtime::reduceControlDependencies(draft);
        },
        nullptr,
        nullptr,
        mayReduceOrder);
}

const camel::runtime::Program *simplifyGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    const OptimizeRewriteConfig &baseConfig, std::ostream &os, size_t maxRounds,
    GraphIRPass *caller) {
    // One growth budget for all rounds: each round unrolls a recursion with constant arguments
    // one level further, so a budget per round would only be reached after maxRounds of them.
    size_t growthSpent           = 0;
    OptimizeRewriteConfig config = baseConfig;
    if (!config.growthSpentNodes) {
        config.growthSpentNodes = &growthSpent;
    }
    RewriteMemo memo;
    const auto finish = [&](const camel::runtime::Program *result) {
        return rewriteReachableGraphs(
            context,
            result,
            "std::opt::order",
            [](camel::runtime::GraphTransaction &transaction) {
            auto &draft = transaction.edit();
            return camel::runtime::reduceOrderDependencies(draft) +
                   camel::runtime::reduceControlDependencies(draft);
        },
            nullptr,
            &memo,
            mayReduceOrder,
            config.checkScreens);
    };
    for (size_t round = 0; round < maxRounds; ++round) {
        bool optChanged = false;
        program         = applyOptimizeRewritePass(context, program, os, config, &optChanged);
        std::deque<camel::core::mm::RootHandle> roots;
        size_t folded = 0, merged = 0, simplified = 0, removed = 0;
        program = rewriteReachableGraphs(
            context,
            program,
            "std::opt::fold",
            [&](camel::runtime::GraphTransaction &transaction) {
                auto &draft = transaction.edit();
                return foldDraft(transaction, *context, roots, config.constantBudgetBytes);
            },
            &folded,
            &memo,
            mayFold,
            config.checkScreens);
        program = rewriteReachableGraphs(
            context,
            program,
            "std::opt::cse",
            cseDraft,
            &merged,
            &memo,
            mayMerge,
            config.checkScreens);
        // The algebraic stage is the pass itself: std::opt::algebra is the rules library's
        // `rules::simplify` (`rules::simplify strict=true` for safe_fp), so the laws are written once,
        // in stdlib/rules.cml. std::opt orders it after fold, as the library's corpus
        // measurements assume.
        const std::string algebraPass =
            config.safeFloatingPoint ? "std::opt::algebra safe_fp=true" : "std::opt::algebra";
        const PassRunResult algebra = applyRegisteredPass(program, algebraPass, context, os, caller);
        if (algebra.program == nullptr) {
            return nullptr;
        }
        program = algebra.program;
        if (algebra.changed) {
            ++simplified;
        }
        program = rewriteReachableGraphs(
            context,
            program,
            "std::opt::dce",
            [](camel::runtime::GraphTransaction &transaction) { return dceDraft(transaction); },
            &removed,
            &memo,
            mayRemoveDead,
            config.checkScreens);
        if (!optChanged && folded == 0 && merged == 0 && simplified == 0 && removed == 0) {
            CAMEL_LOG_INFO_S("Opt", "std::opt: fixpoint after {} round(s)", round + 1);
            return finish(program);
        }
    }
    CAMEL_LOG_WARN_S(
        "Opt",
        "std::opt: still changing after {} rounds (a recursion of non-static depth?)",
        maxRounds);
    return finish(program);
}

