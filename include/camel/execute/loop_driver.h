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
 * Created: Oct. 19, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The rounds of a LOOP node, once for every interpreter (docs/design/loops, docs/technical/
 * 39_loops.md): the order of a round, the layout of the ports a round binds, the state (a value,
 * a k-tuple, or — when the loop collects — a tuple of the state and one array per collection
 * slot), and what ends the loop are the loop's (design principles 3.10, camel/core/loop_kind.h),
 * not an interpreter's. NodeVM, FastVM's native loop and the macro evaluator each supply only
 * the services below, so a change of the rules (more things to iterate over than arrays, how a
 * collected value is kept) is made here.
 *
 * Each round: the condition first (a `while` or a `for … while`), then the next element (a `for`),
 * then the body, whose result is the next state and — when the loop collects — one value per
 * collection slot. Collection appends to an array the loop owns, so it is O(1) amortized per
 * round: arrays grow geometrically, never by copying the whole tape. The arrays live in the
 * loop's value tuple, which the interpreter roots in the node's slot, so the collector traces
 * them like any array of their element type.
 *
 * The header-only template keeps the loop as fast as the three copies were: the services are
 * inlined.
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/graph.h"
#include "camel/utils/assert.h"

#include <cstddef>
#include <cstdint>

namespace camel::execute {

/// What a loop does, as the interpreters need it.
struct LoopShape {
    camel::runtime::GCGraph *cond = nullptr; // while, for … while; nullptr for `for`
    camel::runtime::GCGraph *body = nullptr;
    bool iterates                 = false; // for, for … while: one element a round
    size_t stateCount             = 1;     // k
    /// s: the typed values the body produces for the loop to collect each round (0 normally).
    size_t collectionCount = 0;
    /// The loop's value tuple when it wraps its state (k > 1 or it collects): the k state values,
    /// then one array per collection slot. nullptr when the value is the state itself (k == 1,
    /// no collection). LoopView::valueType() is the same type.
    const camel::core::type::TupleType *valueType = nullptr;
    /// The source origin of the LOOP node: a memory exhausted while the loop collects is told at
    /// the loop.
    uint64_t origin = 0;

    bool valueIsTuple() const { return stateCount > 1 || collectionCount != 0; }
    bool collecting() const { return collectionCount != 0; }
};

/// The j-th value of a running state: the state itself for k = 1 without collection, the tuple's
/// component otherwise. The layout lives here so no interpreter re-derives it.
inline slot_t loopStateComponent(const LoopShape &shape, slot_t value, size_t j) {
    if (!shape.valueIsTuple()) {
        ASSERT(j == 0, "A loop without a value tuple has one state value.");
        return value;
    }
    return camel::core::rtdata::fromSlot<::Tuple *>(value)->get<slot_t>(j);
}

/// Binds the ports of a round of `target` (the condition or the body): the element (when the
/// graph takes one), the k values of the state, then the captures. `stateAt(j)` is the j-th value
/// of the state and `captureAt(c)` the c-th captured value, both read when the round starts.
template <typename StateAt, typename CaptureAt>
inline void bindLoopRound(
    camel::core::context::Frame *frame, camel::runtime::GCGraph *target, size_t stateCount,
    const slot_t *element, size_t captureCount, StateAt &&stateAt, CaptureAt &&captureAt) {
    const auto norm = target->normPorts();
    size_t port     = 0;
    if (element != nullptr) {
        frame->set(target->node(norm[port++])->dataIndex, *element);
    }
    for (size_t j = 0; j < stateCount; ++j) {
        frame->set(target->node(norm[port + j])->dataIndex, stateAt(j));
    }
    const auto with = target->withPorts();
    for (size_t c = 0; c < captureCount; ++c) {
        frame->set(target->node(with[c])->dataIndex, captureAt(c));
    }
}

namespace detail {

/// A fresh empty array for a collection slot. Its element type is in the loop's value type,
/// through which the collector traces it.
inline ::Array *newCollectionArray() { return ::Array::create(camel::core::mm::autoSpace(), 0); }

} // namespace detail

/**
 * Runs the rounds of a loop whose state `host` does not hold yet. The host provides:
 *
 *   void beforeRound(size_t round)            a round is about to start (a limit, a safepoint)
 *   slot_t initialState(size_t j)             the j-th first value of the state, read from the
 *                                             graph before the loop starts
 *   slot_t stateValue()                       the state (the tuple when valueIsTuple())
 *   void setStateValue(slot_t value)          makes `value` the state, rooted
 *   slot_t callRound(const LoopShape &shape, GCGraph *target, const slot_t *element)
 *                                             one call of the condition or of the body for this
 *                                             round: the interpreter's own frame, recursion check
 *                                             and call, with the ports bound (bindLoopRound, the
 *                                             state read through loopStateComponent from
 *                                             stateValue()); returns the graph's result
 *   Array *iterable()                         the array a `for` iterates, read at each round
 */
template <typename Host> inline void driveLoopRounds(const LoopShape &shape, Host &host) {
    const size_t k = shape.stateCount;

    if (!shape.collecting()) {
        // The state as before collection existed: the value when k = 1, a k-tuple if not.
        if (k == 1) {
            host.setStateValue(host.initialState(0));
        } else {
            ::Tuple *state = ::Tuple::create(k, camel::core::mm::autoSpace());
            for (size_t j = 0; j < k; ++j) {
                state->set(j, host.initialState(j), shape.valueType);
            }
            host.setStateValue(camel::core::rtdata::toSlot(state));
        }
        for (size_t round = 0;; ++round) {
            host.beforeRound(round);
            if (shape.cond != nullptr &&
                !camel::core::rtdata::fromSlot<bool>(host.callRound(shape, shape.cond, nullptr))) {
                break;
            }
            slot_t element = NullSlot;
            if (shape.iterates) {
                ::Array *array = host.iterable();
                if (round >= array->size()) {
                    break;
                }
                element = array->data()[round];
            }
            const slot_t next =
                host.callRound(shape, shape.body, shape.iterates ? &element : nullptr);
            host.setStateValue(next);
        }
        return;
    }

    // A collecting loop's state is (state_0 … state_{k-1}, tape_0 … tape_{s-1}) and stays in
    // that one tuple: the tapes are appended to, never copied.
    ASSERT(
        shape.valueType != nullptr && shape.valueType->size() == k + shape.collectionCount,
        "A collecting loop's value type does not have its k + s components.");
    ::Tuple *work = ::Tuple::create(k + shape.collectionCount, camel::core::mm::autoSpace());
    for (size_t j = 0; j < k; ++j) {
        work->set(j, host.initialState(j), shape.valueType);
    }
    for (size_t c = 0; c < shape.collectionCount; ++c) {
        work->set(k + c, detail::newCollectionArray(), shape.valueType);
    }
    host.setStateValue(camel::core::rtdata::toSlot(work));

    for (size_t round = 0;; ++round) {
        host.beforeRound(round);
        if (shape.cond != nullptr &&
            !camel::core::rtdata::fromSlot<bool>(host.callRound(shape, shape.cond, nullptr))) {
            break;
        }
        slot_t element = NullSlot;
        if (shape.iterates) {
            ::Array *array = host.iterable();
            if (round >= array->size()) {
                break;
            }
            element = array->data()[round];
        }
        const slot_t result =
            host.callRound(shape, shape.body, shape.iterates ? &element : nullptr);
        const auto *produced = camel::core::rtdata::fromSlot<::Tuple *>(result);
        auto *state          = camel::core::rtdata::fromSlot<::Tuple *>(host.stateValue());
        for (size_t j = 0; j < k; ++j) {
            state->set(j, produced->get<slot_t>(j), shape.valueType);
        }
        for (size_t c = 0; c < shape.collectionCount; ++c) {
            const auto *arrayType =
                static_cast<const camel::core::type::ArrayType *>(shape.valueType->typeAt(k + c));
            auto *array = state->get<::Array *>(k + c);
            try {
                array->append(produced->get<slot_t>(k + c), arrayType);
            } catch (const camel::core::mm::MemoryExhausted &e) {
                // Growing a tape is the loop's allocation: report the limit at the loop rather
                // than as a bare pass failure.
                throw e.during("collecting the values a loop produces each round", shape.origin);
            }
        }
    }
}

} // namespace camel::execute