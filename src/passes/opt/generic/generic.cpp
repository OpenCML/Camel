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

#include "camel/core/mm/root_handle.h"
#include "camel/execute/executor.h"
#include "camel/runtime/draft_inline.h"

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

/// Static value of a DATA node with a static slot.
std::optional<std::pair<slot_t, type::Type *>>
staticValueOf(const GraphDraft &draft, gc_node_ref_t id) {
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

size_t foldDraft(
    GraphDraft &draft, camel::core::context::Context &ctx,
    std::deque<camel::core::mm::RootHandle> &roots) {
    size_t folded = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            // An element projected out of a tuple built in this graph is the value filled there.
            if (const auto *h = draft.header(id);
                h && h->kind == GCNodeKind::Accs && isReplaceable(draft, id)) {
                const gc_node_ref_t value = camel::runtime::resolveTupleProjection(draft, id);
                if (value != id) {
                    replaceNode(draft, id, value);
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
            // keeps every ordering it expressed. A GATE must keep a control input, so a SYNC
            // without predecessors stays when it is some GATE's only one.
            const auto *h     = draft.header(id);
            bool isJoin       = h && h->kind == GCNodeKind::Sync;
            if (isJoin && draft.ctrlInputsOf(id).empty()) {
                for (gc_node_ref_t user : draft.ctrlUsersOf(id)) {
                    const auto *u = draft.header(user);
                    if (u && u->kind == GCNodeKind::Gate && draft.ctrlInputsOf(user).size() == 1) {
                        isJoin = false;
                        break;
                    }
                }
            }
            if ((isValueOnly(draft, id) || isJoin) && draft.normUsersOf(id).empty() &&
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
