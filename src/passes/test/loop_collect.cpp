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
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The passes of loop_collect.h.
 */

#include "camel/passes/test/loop_collect.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/loop.h"
#include "camel/utils/assert.h"

#include <array>
#include <format>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::runtime::DraftEdgeKind;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;
using camel::runtime::GraphDraft;
using camel::runtime::kInvalidNodeRef;
using camel::runtime::LoopView;
using camel::runtime::RuntimeGraphDraftSession;

gc_node_ref_t addAccs(GraphDraft &draft, gc_node_ref_t tuple, Type *type, uint32_t index) {
    const gc_node_ref_t node = draft.addAccsNode(type, index);
    draft.setNormInputs(node, std::span<const gc_node_ref_t>(&tuple, 1));
    return node;
}

gc_node_ref_t addOper(
    GraphDraft &draft, Type *type, std::string_view uri, std::span<const gc_node_ref_t> inputs) {
    const gc_node_ref_t node = draft.addOperNode(type, nullptr, uri);
    draft.setNormInputs(node, inputs);
    return node;
}

gc_node_ref_t addInt(GraphDraft &draft, int64_t value) {
    return draft.materializeStaticValue(camel::core::rtdata::toSlot<int64_t>(value), Type::Int64());
}

void setResult(GraphDraft &draft, gc_node_ref_t result) {
    draft.setOutputNode(result);
    draft.setExitNode(result);
    draft.setReturnNode(result, camel::runtime::GCReturnKind::Self);
}

/// The value of the LOOP node after the rewrite: (state…, array_0, array_1).
Type *collectedValueType(const std::vector<Type *> &stateTypes, Type *elem) {
    std::vector<Type *> components = stateTypes;
    components.push_back(ArrayType::create(elem));
    components.push_back(ArrayType::create(elem));
    return TupleType::create(std::move(components));
}

/// Counts what one pass changed; the caller commits when it is not zero.
size_t rewriteGraph(RuntimeGraphDraftSession &session, GCGraph *owner, bool summary) {
    std::vector<gc_node_ref_t> loops;
    for (gc_node_ref_t id = 0; id < owner->nodeCount(); ++id) {
        const auto *node = owner->node(id);
        if (node != nullptr && node->kind == camel::runtime::GCNodeKind::Loop) {
            loops.push_back(id);
        }
    }

    size_t changed = 0;
    for (gc_node_ref_t id : loops) {
        const LoopView view(*owner, id);
        if (view.collectionCount() != 0) {
            continue; // already a collecting loop (the pass ran before)
        }
        if (summary && view.stateCount() != 1) {
            throw std::runtime_error(
                "test::loop_collect_summary writes (state, len, len) and needs a one-value state");
        }
        const uint64_t origin = owner->nodeOrigin(id);
        const size_t k        = view.stateCount();
        // A tape holds one value per round. A loop whose state the compiler writes in place
        // (Builder::markLoopStateInPlace) has a single array that every round appends to, so
        // every tape entry would show the final array: the tape needs the copying form, and the
        // body falls back to it. The program is legal either way, so this is a fallback and not
        // a refusal (design principles: an optimization never makes a legal program fail).
        GraphDraft &body = session.edit(view.bodyGraph());
        camel::runtime::fallBackToCopyingAppend(body);
        std::vector<Type *> stateTypes;
        for (size_t j = 0; j < k; ++j) {
            stateTypes.push_back(view.stateType(j));
        }
        Type *const elem      = stateTypes.front();
        const bool increments = elem != nullptr && elem->equals(Type::Int64());

        // The body: return (state_0 … state_{k-1}, next_state_0, next_state_0 + 1).
        const gc_node_ref_t produced = body.draftIdOfSourceRef(view.nextState());
        ASSERT(produced != kInvalidNodeRef, "A LOOP body's result is not in its draft.");
        std::vector<gc_node_ref_t> values;
        std::vector<size_t> slots;
        for (size_t j = 0; j < k; ++j) {
            values.push_back(
                k == 1 ? produced
                       : addAccs(body, produced, stateTypes[j], static_cast<uint32_t>(j)));
            slots.push_back(j);
        }
        const gc_node_ref_t slot0 = values.front();
        const gc_node_ref_t slot1 = increments
                                        ? addOper(
                                              body,
                                              Type::Int64(),
                                              ":op/add_l",
                                              std::array<gc_node_ref_t, 2>{slot0, addInt(body, 1)})
                                        : slot0;
        values.push_back(slot0);
        slots.push_back(k);
        values.push_back(slot1);
        slots.push_back(k + 1);

        std::vector<Type *> bodyComponents = stateTypes;
        bodyComponents.push_back(elem);
        bodyComponents.push_back(elem);
        auto *const bodyType       = TupleType::create(std::move(bodyComponents));
        auto *const empty          = ::Tuple::create(k + 2, camel::core::mm::autoSpace());
        const gc_node_ref_t source = body.materializeStaticValue(
            camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(empty),
            bodyType);
        const gc_node_ref_t fill = body.addFillNode(
            bodyType,
            camel::runtime::makeFillPayload(camel::runtime::GCFillKind::Tuple, slots));
        body.setNormInputs(fill, std::span<const gc_node_ref_t>(&source, 1));
        body.setWithInputs(fill, values);
        body.setNodeOrigin(fill, origin);
        body.setFuncType(body.funcType()->withExitType(bodyType));
        setResult(body, fill);

        // The node: its value is the state and the two tapes.
        GraphDraft &ownerDraft      = session.edit(owner);
        const gc_node_ref_t draftId = ownerDraft.draftIdOfSourceRef(id);
        ASSERT(draftId != kInvalidNodeRef, "A LOOP node is not in its graph's draft.");
        Type *const valueType = collectedValueType(stateTypes, elem);
        ownerDraft.setNodeDataType(draftId, valueType);
        ownerDraft.setNodeDataIndex(draftId, ownerDraft.allocateRuntimeSlot(valueType));
        ownerDraft.setLoopCollectionCount(draftId, 2);

        if (summary) {
            // The loop's users keep seeing a compact value: (state, len(tape0), len(tape1)).
            // The original users are recorded before the projection itself uses the node.
            std::vector<gc_node_ref_t> normUsers(
                ownerDraft.normUsersOf(draftId).begin(),
                ownerDraft.normUsersOf(draftId).end());
            std::vector<gc_node_ref_t> withUsers(
                ownerDraft.withUsersOf(draftId).begin(),
                ownerDraft.withUsersOf(draftId).end());
            const bool isOutput = ownerDraft.outputNode() == draftId;
            const bool isExit   = ownerDraft.exitNode() == draftId;
            const bool isReturn = ownerDraft.returnNode() == draftId;

            const auto *const valueTuple = static_cast<const TupleType *>(valueType);
            const gc_node_ref_t state    = addAccs(ownerDraft, draftId, stateTypes.front(), 0);
            const gc_node_ref_t tape0 =
                addAccs(ownerDraft, draftId, valueTuple->typeAt(1), static_cast<uint32_t>(1));
            const gc_node_ref_t tape1 =
                addAccs(ownerDraft, draftId, valueTuple->typeAt(2), static_cast<uint32_t>(2));
            const gc_node_ref_t len0 = addOper(
                ownerDraft,
                Type::Int64(),
                ":struct/len_arr",
                std::span<const gc_node_ref_t>(&tape0, 1));
            const gc_node_ref_t len1 = addOper(
                ownerDraft,
                Type::Int64(),
                ":struct/len_arr",
                std::span<const gc_node_ref_t>(&tape1, 1));
            std::vector<Type *> summaryTypes{stateTypes.front(), Type::Int64(), Type::Int64()};
            auto *const summaryType           = TupleType::create(std::move(summaryTypes));
            auto *const summaryEmpty          = ::Tuple::create(3, camel::core::mm::autoSpace());
            const gc_node_ref_t summarySource = ownerDraft.materializeStaticValue(
                camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(summaryEmpty),
                summaryType);
            const std::array<gc_node_ref_t, 3> summaryValues{state, len0, len1};
            const gc_node_ref_t summary = ownerDraft.addFillNode(
                summaryType,
                camel::runtime::makeFillPayload(
                    camel::runtime::GCFillKind::Tuple,
                    std::array<size_t, 3>{0, 1, 2}));
            ownerDraft.setNormInputs(summary, std::span<const gc_node_ref_t>(&summarySource, 1));
            ownerDraft.setWithInputs(summary, summaryValues);
            ownerDraft.setNodeOrigin(summary, origin);

            for (gc_node_ref_t user : normUsers) {
                ownerDraft.replaceInput(DraftEdgeKind::Norm, user, draftId, summary);
            }
            for (gc_node_ref_t user : withUsers) {
                ownerDraft.replaceInput(DraftEdgeKind::With, user, draftId, summary);
            }
            if (isOutput) {
                ownerDraft.setOutputNode(summary);
            }
            if (isExit) {
                ownerDraft.setExitNode(summary);
            }
            if (isReturn) {
                ownerDraft.setReturnNode(summary, ownerDraft.returnKind());
            }
        }
        ++changed;
    }
    return changed;
}

const camel::runtime::Program *applyCollect(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    bool summary) {
    if (program == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context, program);
    const std::vector<GCGraph *> graphs = session.collectReachableRuntimeGraphs();
    size_t rewrites                     = 0;
    for (GCGraph *owner : graphs) {
        rewrites += rewriteGraph(session, owner, summary);
    }
    return rewrites == 0 ? program : session.commit();
}

} // namespace

const camel::runtime::Program *
LoopCollectTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return applyCollect(context_, program, false);
}

const camel::runtime::Program *
LoopCollectSummaryTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return applyCollect(context_, program, true);
}