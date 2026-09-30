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
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The generic optimization passes std::opt::{fold, cse, dce} (see generic.h).
 */

#include "generic.h"

#include "rewrite.h"

#include "../inline/engine.h"

#include "camel/core/mm/root_handle.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/base.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/struct.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_types.h"

#include <deque>
#include <map>
#include <optional>
#include <queue>
#include <tuple>
#include <vector>

using namespace camel::passes::generic;
using camel::runtime::DraftBrchPayload;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::kInvalidNodeRef;
namespace type = camel::core::type;

namespace {

// ---------------------------------------------------------------- branch regions

/**
 * For every node, the branch arms it lies in (token = brch << 16 | arm, sorted). Arms are inline
 * regions: nodes forward-reachable from an arm's head, up to the branch's JOIN. Two nodes may
 * only be merged when they lie in exactly the same arms, so the survivor runs whenever the
 * replaced node would have.
 */
std::vector<std::vector<uint64_t>> armRegions(const GraphDraft &draft) {
    std::vector<std::vector<uint64_t>> regions(draft.nodeSlotCount());
    for (gc_node_ref_t brch = 0; brch < draft.nodeSlotCount(); ++brch) {
        const auto *h = draft.header(brch);
        if (!h || h->kind != GCNodeKind::Brch) {
            continue;
        }
        const auto join =
            reinterpret_cast<const DraftBrchPayload *>(draft.payloadOf(brch).data())->join;
        const auto arms = draft.branchArmsOf(brch);
        for (size_t arm = 0; arm < arms.size(); ++arm) {
            const uint64_t token = (static_cast<uint64_t>(brch) << 16) | arm;
            std::vector<bool> seen(draft.nodeSlotCount(), false);
            std::queue<gc_node_ref_t> work;
            if (arms[arm].head != kInvalidNodeRef) {
                work.push(arms[arm].head);
            }
            while (!work.empty()) {
                const gc_node_ref_t n = work.front();
                work.pop();
                if (n == join || n == brch || seen[n] || !draft.header(n)) {
                    continue;
                }
                seen[n] = true;
                regions[n].push_back(token);
                for (auto users :
                     {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
                    for (gc_node_ref_t u : users) {
                        work.push(u);
                    }
                }
            }
        }
    }
    for (auto &r : regions) {
        std::ranges::sort(r);
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

/// Static value of a DATA node with a static slot, also seen through GATEs forwarding one (an
/// inlined call gates its arguments; the value it forwards is the same).
std::optional<std::pair<slot_t, type::Type *>>
staticValueOf(const GraphDraft &draft, gc_node_ref_t id) {
    for (size_t hops = 0; hops < draft.nodeSlotCount(); ++hops) {
        const auto *gate = draft.header(id);
        if (!gate || gate->kind != GCNodeKind::Gate || draft.normInputsOf(id).empty()) {
            break;
        }
        id = draft.normInputsOf(id).back();
    }
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Data || h->dataIndex >= 0) {
        return std::nullopt;
    }
    const auto index = static_cast<size_t>(-h->dataIndex);
    if (index >= draft.staticSlots().size()) {
        return std::nullopt;
    }
    type::Type *ty = draft.staticSlotTypes()[index];
    return std::pair{draft.staticSlots()[index], ty ? ty : h->dataType};
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
 * The nodes of each arm of `brch`, or nullopt when they cannot be told apart. A branch's code is
 * what runs after the BRCH and before its JOIN (forward-reachable from the BRCH, stopping at the
 * JOIN); an arm's part of it is what its tail depends on. Inlining can leave several nodes of an
 * arm waiting on the BRCH directly, so arms are not identified by their heads alone. Every node of
 * the branch must belong to exactly one arm; otherwise the branch is left alone.
 */
std::optional<std::vector<std::vector<gc_node_ref_t>>> branchArmNodes(
    const GraphDraft &draft, gc_node_ref_t brch, gc_node_ref_t join,
    std::span<const gc_node_ref_t> tails) {
    std::vector<bool> inBranch(draft.nodeSlotCount(), false);
    std::vector<gc_node_ref_t> work;
    for (auto users : {draft.normUsersOf(brch), draft.withUsersOf(brch), draft.ctrlUsersOf(brch)}) {
        work.insert(work.end(), users.begin(), users.end());
    }
    while (!work.empty()) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (n == join || n == brch || inBranch[n] || !draft.header(n)) {
            continue;
        }
        inBranch[n] = true;
        for (auto users : {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
            work.insert(work.end(), users.begin(), users.end());
        }
    }
    std::vector<int> owner(draft.nodeSlotCount(), -1);
    std::vector<std::vector<gc_node_ref_t>> regions(tails.size());
    for (size_t arm = 0; arm < tails.size(); ++arm) {
        std::vector<gc_node_ref_t> stack;
        if (tails[arm] < inBranch.size() && inBranch[tails[arm]]) {
            stack.push_back(tails[arm]);
        }
        while (!stack.empty()) {
            const gc_node_ref_t n = stack.back();
            stack.pop_back();
            if (owner[n] == static_cast<int>(arm)) {
                continue;
            }
            if (owner[n] != -1) {
                return std::nullopt; // shared by two arms
            }
            owner[n] = static_cast<int>(arm);
            regions[arm].push_back(n);
            for (auto inputs : {draft.normInputsOf(n), draft.withInputsOf(n), draft.ctrlInputsOf(n)}) {
                for (gc_node_ref_t in : inputs) {
                    if (in < inBranch.size() && inBranch[in]) {
                        stack.push_back(in);
                    }
                }
            }
        }
    }
    for (gc_node_ref_t n = 0; n < inBranch.size(); ++n) {
        if (inBranch[n] && owner[n] == -1) {
            return std::nullopt; // runs in the branch but feeds no arm's result
        }
    }
    return regions;
}

/**
 * Replaces a branch whose arm is statically known by that arm's code: the other arms are removed,
 * the JOIN's users read the taken arm's result, and the taken arm runs after what the BRCH ran
 * after. Returns false (and changes nothing) for shapes it does not handle.
 */
bool pruneBranch(GraphDraft &draft, gc_node_ref_t brch) {
    const auto arm = staticArmOf(draft, brch);
    if (!arm) {
        return false;
    }
    const gc_node_ref_t join =
        reinterpret_cast<const DraftBrchPayload *>(draft.payloadOf(brch).data())->join;
    const std::vector<camel::runtime::GCBranchArm> arms(
        draft.branchArmsOf(brch).begin(),
        draft.branchArmsOf(brch).end());
    const auto *joinHeader = draft.header(join);
    if (*arm >= arms.size() || !joinHeader || joinHeader->kind != GCNodeKind::Join ||
        draft.withInputsOf(join).size() != arms.size()) {
        return false;
    }
    const gc_node_ref_t result = draft.withInputsOf(join)[*arm];

    const auto tails = draft.withInputsOf(join);
    auto found = branchArmNodes(draft, brch, join, tails);
    if (!found) {
        return false;
    }
    const auto &regions = *found;
    // The arms' code must stay inside the branch: nothing but the JOIN may read it.
    for (const auto &region : regions) {
        for (gc_node_ref_t n : region) {
            for (auto users : {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
                for (gc_node_ref_t u : users) {
                    if (u != join && std::ranges::find(region, u) == region.end()) {
                        return false;
                    }
                }
            }
        }
    }

    const std::vector<gc_node_ref_t> brchPreds(
        draft.ctrlInputsOf(brch).begin(),
        draft.ctrlInputsOf(brch).end());

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
    // A removed node must not anchor another branch that stays (inlining can make one value the
    // result of two branches).
    for (gc_node_ref_t other = 0; other < draft.nodeSlotCount(); ++other) {
        const auto *h = draft.header(other);
        if (other == brch || !h || h->kind != GCNodeKind::Brch || dropped[other]) {
            continue;
        }
        for (const auto &a : draft.branchArmsOf(other)) {
            if ((a.head != kInvalidNodeRef && a.head < dropped.size() && dropped[a.head]) ||
                (a.tail != kInvalidNodeRef && a.tail < dropped.size() && dropped[a.tail])) {
                return false;
            }
        }
    }
    const auto &preds = brchPreds;
    const std::vector<gc_node_ref_t> brchCtrlUsers(
        draft.ctrlUsersOf(brch).begin(),
        draft.ctrlUsersOf(brch).end());
    for (gc_node_ref_t user : brchCtrlUsers) {
        if (user == join || removed(user)) {
            continue;
        }
        draft.unlinkInput(camel::runtime::DraftEdgeKind::Ctrl, user, brch);
        for (gc_node_ref_t pred : preds) {
            const auto existing = draft.ctrlInputsOf(user);
            if (pred != user && std::ranges::find(existing, pred) == existing.end()) {
                draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, user, pred);
            }
        }
    }

    // The JOIN's value users read the taken result. Its control users ran after the whole
    // branch: they now wait for the result, for what the JOIN itself waited on, and for what the
    // BRCH waited on (the result may come from outside the arm, so waiting on it alone could cut
    // them off from earlier effects).
    const std::vector<gc_node_ref_t> joinCtrlUsers(
        draft.ctrlUsersOf(join).begin(),
        draft.ctrlUsersOf(join).end());
    std::vector<gc_node_ref_t> after{result};
    for (gc_node_ref_t pred : draft.ctrlInputsOf(join)) {
        after.push_back(pred);
    }
    after.insert(after.end(), brchPreds.begin(), brchPreds.end());
    draft.replaceAllValueUses(join, result);
    for (gc_node_ref_t user : joinCtrlUsers) {
        draft.unlinkInput(camel::runtime::DraftEdgeKind::Ctrl, user, join);
        for (gc_node_ref_t pred : after) {
            const auto existing = draft.ctrlInputsOf(user);
            if (pred != user && !dropped[pred] && std::ranges::find(existing, pred) == existing.end()) {
                draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, user, pred);
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

    // An enclosing branch may be anchored on this one: its arm starts at the BRCH or ends at the
    // JOIN. It now starts where the taken arm starts and ends at the taken result.
    const gc_node_ref_t takenHead = arms[*arm].head;
    const gc_node_ref_t newHead =
        takenHead != kInvalidNodeRef && draft.header(takenHead) && !dropped[takenHead] ? takenHead
                                                                                          : result;
    draft.retargetBranchArmAnchors(join, result, result);
    draft.retargetBranchArmAnchors(brch, newHead, result);
    draft.eraseNode(join);
    draft.eraseNode(brch);
    for (gc_node_ref_t n = 0; n < draft.nodeSlotCount(); ++n) {
        if (dropped[n] && draft.header(n)) {
            draft.eraseNode(n);
        }
    }
    // Gates that waited only on the branch now wait on nothing; commit dissolves them.
    return true;
}

/// Replaces an element access on a constant tuple or struct by the element's value.
bool foldStaticAccess(GraphDraft &draft, gc_node_ref_t id) {
    const auto sources = draft.normInputsOf(id);
    if (sources.size() != 1) {
        return false;
    }
    const auto source = staticValueOf(draft, sources[0]);
    type::Type *result = draft.header(id)->dataType;
    if (!source || !source->second || !result || source->first == NullSlot) {
        return false;
    }
    const auto *body = reinterpret_cast<const camel::runtime::GCAccsBody *>(draft.payloadOf(id).data());
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
    // The element is reachable from the constant it was read from, which the graph keeps.
    replaceNode(draft, id, draft.materializeStaticValue(value, result));
    return true;
}

/// Replaces a FILL whose template and values are all constants by the object it builds.
bool foldStaticFill(
    GraphDraft &draft, gc_node_ref_t id, std::deque<camel::core::mm::RootHandle> &roots) {
    const auto sources = draft.normInputsOf(id);
    type::Type *result = draft.header(id)->dataType;
    if (sources.size() != 1 || !result || !type::isGCTraced(result->code())) {
        return false;
    }
    const auto *body = reinterpret_cast<const camel::runtime::GCFillBody *>(draft.payloadOf(id).data());
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
    auto *object = camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(source->first)
                       ->clone(camel::core::mm::autoSpace(), result, false);
    // Keep the object alive until commit moves it into the graph's static area.
    roots.emplace_back(camel::core::mm::autoSpace(), object, result, "std::opt::fold");
    camel::execute::writeRuntimeFillSlots(object, result, body, slots);
    replaceNode(draft, id, draft.materializeStaticValue(camel::core::rtdata::toSlot(object), result));
    return true;
}

/// Replaces a pure operator by the constant its argument types fix, through the operator's
/// registered type folder. Returns false when there is none or the types do not fix the result.
bool foldFromTypes(
    GraphDraft &draft, gc_node_ref_t id, std::deque<camel::core::mm::RootHandle> &roots) {
    const auto *folder =
        camel::core::OperatorTypeFolderRegistry::instance().find(operUriOf(draft, id));
    type::Type *resultType = draft.header(id)->dataType;
    if (!folder || !resultType || !draft.withInputsOf(id).empty()) {
        return false;
    }
    std::vector<type::Type *> types;
    std::vector<std::optional<slot_t>> statics;
    for (gc_node_ref_t in : draft.normInputsOf(id)) {
        const auto *h = draft.header(in);
        if (!h || !h->dataType) {
            return false;
        }
        types.push_back(h->dataType);
        const auto value = staticValueOf(draft, in);
        statics.push_back(value ? std::optional<slot_t>(value->first) : std::nullopt);
    }
    const auto result = (*folder)(types, statics, camel::core::mm::autoSpace());
    if (!result) {
        return false;
    }
    if (type::isGCTraced(resultType->code()) && *result != NullSlot) {
        // Keep the value alive until commit moves it into the graph's static area.
        roots.emplace_back(
            camel::core::mm::autoSpace(),
            camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(*result),
            resultType,
            "std::opt::fold");
    }
    replaceNode(draft, id, draft.materializeStaticValue(*result, resultType));
    return true;
}

size_t foldDraft(
    GraphDraft &draft, camel::core::context::Context &ctx,
    std::deque<camel::core::mm::RootHandle> &roots) {
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
                h && h->kind == GCNodeKind::Accs && isReplaceable(draft, id)) {
                const gc_node_ref_t value = camel::runtime::resolveTupleProjection(draft, id);
                if (value != id) {
                    replaceNode(draft, id, value);
                    ++folded;
                    changed = true;
                } else if (foldStaticAccess(draft, id)) {
                    ++folded;
                    changed = true;
                }
                continue;
            }
            // A tuple, struct or array built from constants is a constant.
            if (const auto *h = draft.header(id);
                h && h->kind == GCNodeKind::Fill && isReplaceable(draft, id)) {
                if (foldStaticFill(draft, id, roots)) {
                    ++folded;
                    changed = true;
                }
                continue;
            }
            if (!isPureOper(draft, id) || !isReplaceable(draft, id)) {
                continue;
            }
            auto with = staticArgs(draft, draft.withInputsOf(id));
            auto norm = staticArgs(draft, draft.normInputsOf(id));
            if (!with || !norm) {
                // Not all arguments are constants, but their types may fix the result (e.g. the
                // shape of a tensor whose type carries it).
                if (foldFromTypes(draft, id, roots)) {
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
            slot_t result          = NullSlot;
            try {
                result = (*op)(*with, *norm, ctx);
            } catch (...) {
                continue; // leave the failure to be reported when the program runs
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
            replaceNode(draft, id, value);
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

size_t cseDraft(GraphDraft &draft) {
    const auto regions = armRegions(draft);
    using Key          = std::tuple<
                 std::string,
                 type::Type *,
                 std::vector<gc_node_ref_t>,
                 std::vector<gc_node_ref_t>,
                 std::vector<uint64_t>>;
    std::map<Key, gc_node_ref_t> seen;
    size_t merged = 0;

    // Equal scalar constants are one value: merge their DATA nodes first, so expressions over
    // separately written literals (`n * 7` twice) become identical. GC-traced constants keep
    // their identity (objects may be compared by reference).
    std::map<std::tuple<type::Type *, slot_t, std::vector<uint64_t>>, gc_node_ref_t> constants;
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        const auto value = staticValueOf(draft, id);
        if (!value || !value->second || type::isGCTraced(value->second->code())) {
            continue;
        }
        auto [it, inserted] = constants.try_emplace({value->second, value->first, regions[id]}, id);
        if (!inserted && isReplaceable(draft, id) && !reaches(draft, id, it->second)) {
            replaceNode(draft, id, it->second);
            ++merged;
        }
    }

    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!isPureOper(draft, id)) {
            continue;
        }
        const auto norm = draft.normInputsOf(id);
        const auto with = draft.withInputsOf(id);
        Key key{
            std::string(operUriOf(draft, id)),
            draft.header(id)->dataType,
            std::vector<gc_node_ref_t>(norm.begin(), norm.end()),
            std::vector<gc_node_ref_t>(with.begin(), with.end()),
            regions[id]};
        auto [it, inserted] = seen.try_emplace(std::move(key), id);
        if (!inserted && isReplaceable(draft, id) && !reaches(draft, id, it->second)) {
            replaceNode(draft, id, it->second);
            ++merged;
        }
    }
    return merged;
}

// ---------------------------------------------------------------- dce

size_t dceDraft(GraphDraft &draft) {
    size_t removed = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            // A SYNC only joins control; replaceNode hands its predecessors to its users, which
            // keeps every ordering it expressed.
            // A GATE only forwards its value once its control inputs ran; unread, it is dead.
            const auto *h     = draft.header(id);
            const bool isJoin = h && h->kind == GCNodeKind::Sync;
            const bool isDeadGate =
                h && h->kind == GCNodeKind::Gate && draft.ctrlUsersOf(id).empty();
            if ((isValueOnly(draft, id) || isJoin || isDeadGate) && draft.normUsersOf(id).empty() &&
                draft.withUsersOf(id).empty() && isReplaceable(draft, id)) {
                replaceNode(draft, id, kInvalidNodeRef);
                ++removed;
                changed = true;
            }
        }
    }
    return removed;
}

} // namespace

GCGraph *ConstantFoldPass::apply(GCGraph *graph, std::ostream &) {
    std::deque<camel::core::mm::RootHandle> roots;
    return rewriteReachableGraphs(context_, graph, "std::opt::fold", [&](GraphDraft &draft) {
        return foldDraft(draft, *context_, roots);
    });
}

GCGraph *CommonSubexpressionPass::apply(GCGraph *graph, std::ostream &) {
    return rewriteReachableGraphs(context_, graph, "std::opt::cse", cseDraft);
}

GCGraph *DeadCodePass::apply(GCGraph *graph, std::ostream &) {
    return rewriteReachableGraphs(context_, graph, "std::opt::dce", dceDraft);
}

GCGraph *simplifyGraph(
    const camel::core::context::context_ptr_t &context, GCGraph *graph,
    const OptimizeRewriteConfig &config, std::ostream &os, size_t maxRounds) {
    for (size_t round = 0; round < maxRounds; ++round) {
        bool optChanged = false;
        graph = applyOptimizeRewritePass(context, graph, os, config, &optChanged);
        std::deque<camel::core::mm::RootHandle> roots;
        size_t folded = 0, removed = 0;
        graph = rewriteReachableGraphs(
            context,
            graph,
            "std::opt::fold",
            [&](GraphDraft &draft) { return foldDraft(draft, *context, roots); },
            &folded);
        graph = rewriteReachableGraphs(context, graph, "std::opt::dce", dceDraft, &removed);
        if (!optChanged && folded == 0 && removed == 0) {
            CAMEL_LOG_INFO_S("Opt", "std::opt::simplify: fixpoint after {} round(s)", round + 1);
            return graph;
        }
    }
    CAMEL_LOG_WARN_S(
        "Opt",
        "std::opt::simplify: still changing after {} rounds (a recursion of non-static depth?)",
        maxRounds);
    return graph;
}

GCGraph *SimplifyPass::apply(GCGraph *graph, std::ostream &os) {
    return simplifyGraph(context_, graph, OptimizeRewriteConfig{}, os);
}
