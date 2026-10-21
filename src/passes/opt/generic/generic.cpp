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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Generic optimization passes and the integrated pipeline (algebra lives in algebra.cpp).
 */

#include "generic.h"

#include "rewrite.h"

#include "../inline/engine.h"

#include "camel/core/mm/root_handle.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/execute/pass/warnings.h"
#include "camel/runtime/control.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_types.h"
#include "camel/runtime/draft_writes.h"

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
/// graph, and nothing writes it in place. A written node is not one value: it is a variable's
/// cell or an object an operator changes in place (a constant's object is shared by every run
/// of the graph), and readers ordered after the writer see what it wrote.
bool isFoldable(const GraphDraft &draft, gc_node_ref_t id) {
    return isReplaceable(draft, id) && !isWrittenInPlace(draft, id);
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
            for (auto users : {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
                for (gc_node_ref_t u : users) {
                    if (u != join && std::ranges::find(region, u) == region.end()) {
                        return false;
                    }
                }
            }
        }
    }

    std::vector<gc_node_ref_t> brchPreds(
        draft.ctrlInputsOf(brch).begin(),
        draft.ctrlInputsOf(brch).end());
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
            camel::runtime::appendControlDependency(draft, pred, user);
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
    auto *object = camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(source->first)
                       ->clone(camel::core::mm::autoSpace(), result, false);
    // Keep the object alive until commit moves it into the graph's static area.
    roots.emplace_back(camel::core::mm::autoSpace(), object, result, "std::opt::fold");
    camel::execute::writeRuntimeFillSlots(object, result, body, slots);
    replaceNode(
        draft,
        id,
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
    GraphDraft &draft, gc_node_ref_t id, std::deque<camel::core::mm::RootHandle> &roots) {
    const auto sources = draft.normInputsOf(id);
    type::Type *target = draft.header(id)->dataType;
    if (sources.size() != 1 || !target || !draft.withInputsOf(id).empty()) {
        return false;
    }
    const auto source = staticValueOf(draft, sources[0]);
    if (!source || !source->second) {
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
    if (!checkedByTarget && !safePrimitive) {
        return false;
    }
    slot_t result = NullSlot;
    try {
        result = target->castSlotFrom(source->first, source->second);
    } catch (...) {
        return false; // the conversion fails: left to be reported when the program runs
    }
    if (type::isGCTraced(target->code()) && result != NullSlot && result != source->first) {
        // Keep a value the conversion made alive until commit moves it into the static area.
        roots.emplace_back(
            camel::core::mm::autoSpace(),
            camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(result),
            target,
            "std::opt::fold");
    }
    replaceNode(draft, id, draft.materializeStaticValue(result, target));
    return true;
}

/// Replaces a pure operator by the constant its argument types fix, through the operator's
/// registered type folder. Returns false when there is none or the types do not fix the result.
bool foldFromTypes(
    GraphDraft &draft, gc_node_ref_t id, std::deque<camel::core::mm::RootHandle> &roots) {
    const auto *folder     = camel::core::OperatorTable::instance().typeFolder(operIdOf(draft, id));
    type::Type *resultType = draft.header(id)->dataType;
    if (!folder || !resultType || !draft.withInputsOf(id).empty()) {
        return false;
    }
    std::vector<type::Type *> types;
    std::vector<type::StaticArg> statics;
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
                h && h->kind == GCNodeKind::Accs && isFoldable(draft, id)) {
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
                h && h->kind == GCNodeKind::Fill && isFoldable(draft, id)) {
                if (foldStaticFill(draft, id, roots)) {
                    ++folded;
                    changed = true;
                }
                continue;
            }
            // A conversion of a constant is a constant, when the conversion succeeds.
            if (const auto *h = draft.header(id);
                h && h->kind == GCNodeKind::Cast && isFoldable(draft, id)) {
                if (foldStaticCast(draft, id, roots)) {
                    ++folded;
                    changed = true;
                }
                continue;
            }
            if (!isPureOper(draft, id)) {
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
                raisePassWarning(
                    ctx,
                    camel::core::error::RuntimeDiag::ConstantExpressionFaults,
                    draft.nodeOrigin(id),
                    fault.what());
                continue;
            } catch (...) {
                continue; // leave the failure to be reported when the program runs
            }
            if (!foldable) {
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
            replaceNode(draft, id, it->second);
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
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!isPureOper(draft, id) || isWrittenInPlace(draft, id) || readsVariable(id)) {
            continue;
        }
        const auto norm = draft.normInputsOf(id);
        const auto with = draft.withInputsOf(id);
        Key key{
            std::string(operUriOf(draft, id)),
            draft.header(id)->dataType,
            std::vector<gc_node_ref_t>(norm.begin(), norm.end()),
            std::vector<gc_node_ref_t>(with.begin(), with.end()),
            regions[id],
            draft.nodePermissions(id)};
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
                replaceNode(draft, id, kept);
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
 * to those users, including an effect whose only current successor is the gate.
 */
bool gateInputsSurvive(const GraphDraft &draft, gc_node_ref_t gate) {
    for (auto inputs :
         {draft.normInputsOf(gate), draft.withInputsOf(gate), draft.ctrlInputsOf(gate)}) {
        for (gc_node_ref_t in : inputs) {
            if (isValueOnly(draft, in)) {
                continue;
            }
            size_t users = 0;
            for (auto group :
                 {draft.normUsersOf(in), draft.withUsersOf(in), draft.ctrlUsersOf(in)}) {
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
size_t dceDraft(GraphDraft &draft, bool valuesOnly = false) {
    size_t removed        = 0;
    size_t gateEdgeBudget = std::min<size_t>(8192, draft.nodeSlotCount() * 8);
    for (bool changed = true; changed;) {
        changed = false;
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (!valuesOnly && bypassValueGate(draft, id, gateEdgeBudget)) {
                ++removed;
                changed = true;
                continue;
            }
            // A SYNC only joins control; replaceNode hands its predecessors to its users, which
            // keeps every ordering it expressed.
            // A GATE only forwards its value once its control inputs ran. When no node reads the
            // value it is a SYNC that also depends on a value, and it goes the same way: the
            // nodes that wait on it wait on what it waited on instead (its control inputs and
            // the ordered nodes its value came from). Keeping such a GATE because other nodes
            // wait on it kept its value alive for nothing: a closure that is only called
            // through a branch stayed read by the dead GATE, and the export could not split
            // that call across the branch. A GATE that nothing uses at all stays when it is the
            // only way something it waits on reaches the exit.
            const auto *h     = draft.header(id);
            const bool isJoin = !valuesOnly && h && h->kind == GCNodeKind::Sync;
            const bool isDeadGate =
                !valuesOnly && h && h->kind == GCNodeKind::Gate &&
                (!draft.ctrlUsersOf(id).empty() || gateInputsSurvive(draft, id));
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

const camel::runtime::Program *
ConstantFoldPass::apply(const camel::runtime::Program *program, std::ostream &) {
    std::deque<camel::core::mm::RootHandle> roots;
    return rewriteReachableGraphs(
        context_,
        program,
        "std::opt::fold",
        [&](GraphDraft &draft) { return foldDraft(draft, *context_, roots); },
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
        [&](GraphDraft &draft) { return dceDraft(draft, valuesOnly_); },
        nullptr,
        nullptr,
        mayRemoveDead);
}

const camel::runtime::Program *
ControlReductionPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return rewriteReachableGraphs(
        context_,
        program,
        "std::opt::ctrl",
        [](GraphDraft &draft) { return camel::runtime::reduceControlDependencies(draft); },
        nullptr,
        nullptr,
        mayReduceControl);
}

const camel::runtime::Program *simplifyGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    const OptimizeRewriteConfig &baseConfig, std::ostream &os, size_t maxRounds) {
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
            "std::opt::ctrl",
            [](GraphDraft &draft) { return camel::runtime::reduceControlDependencies(draft); },
            nullptr,
            &memo,
            mayReduceControl,
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
            [&](GraphDraft &draft) { return foldDraft(draft, *context, roots); },
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
        program = rewriteReachableGraphs(
            context,
            program,
            "std::opt::algebra",
            [&](GraphDraft &draft) { return algebraDraft(draft, config.safeFloatingPoint); },
            &simplified,
            &memo,
            mayAlgebra,
            config.checkScreens);
        program = rewriteReachableGraphs(
            context,
            program,
            "std::opt::dce",
            [](GraphDraft &draft) { return dceDraft(draft); },
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
