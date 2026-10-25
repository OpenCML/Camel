// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The rounds of a LOOP node, once for every interpreter (docs/technical/
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
#include <span>

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

/**
 * The value one round of a `for` hands over, in the form the round's declared type says.
 * `Array::firstDimensionElement` is what the first dimension gives: the element itself for a
 * rank-1 array, the row one dimension fewer for a higher-rank one (design principles 5.4). When
 * that value is the element itself while the round's declared type `elementType` is an array, the
 * element is handed over as the array of no dimension holding it: the sub-tensor one dimension
 * fewer, the same value in the form a reader of an array-typed slot takes. A round of an array
 * whose rank is open (`f32[..]`) is declared as that array — a rank nobody fixed leaves one
 * dimension fewer an open rank too, and no type can say "the element at rank 1, the row above it"
 * (ArrayType::firstDimensionElement) — so its reader takes a pointer there (the tensor module's
 * tensorArg), and a value with no dimension stays an array for a reader whose type says an array,
 * as it does for a result read by one (wrap). Handing the element itself over would leave the slot
 * holding the element's bits where its declared type promises a pointer.
 *
 * The rounds are the same on every back end, so this rule lives here with the rest of the round's
 * rules, and every executor asks it (loop_frame.h's loopTest, driveLoopRounds).
 */
inline slot_t loopRoundValue(
    ::Array *array, uint64_t round, const camel::core::type::Type *elementType) {
    const slot_t element = array->firstDimensionElement(round);
    // An array whose elements are arrays (`f32[][]`, `f32[][..]`): what the first dimension gives
    // is the inner array, which is already the array the round's declared type says.
    const camel::core::type::Type *elementOfElement = array->elemType();
    const bool elementIsAnArray =
        elementOfElement != nullptr &&
        elementOfElement->code() == camel::core::type::TypeCode::Array;
    if (elementType == nullptr || elementType->code() != camel::core::type::TypeCode::Array ||
        array->rank() > 1 || elementIsAnArray) {
        // The declared type is the element itself (a rank-1 array's `f32`, an interval's integer),
        // the first dimension gave the row of a higher-rank array (already an array), or the
        // element is an array: the value the first dimension gives is the round's value.
        return element;
    }
    if (array->isCompact()) {
        // The sub-tensor one dimension fewer of a rank-1 array holds the same element, so it
        // shares the data as a row does (arrays are immutable values, and a view is how a reader
        // of the type reads one).
        return camel::core::rtdata::toSlot(Array::view(array, std::span<const int64_t>(), round));
    }
    // An array of references: the round is a copy of the one element, as a row of such an array is
    // a copy of its run of slots.
    const camel::core::type::ArrayType *boxType =
        elementOfElement != nullptr
            ? camel::core::type::ArrayType::create(
                  const_cast<camel::core::type::Type *>(elementOfElement))
            : static_cast<const camel::core::type::ArrayType *>(elementType);
    ::Array *box = ::Array::create(camel::core::mm::autoSpace(), boxType, std::span<const int64_t>());
    box->setSlot(0, element, static_cast<const camel::core::type::ArrayType *>(elementType));
    return camel::core::rtdata::toSlot(box);
}

namespace detail {

/// A fresh empty array for a collection slot. Its element type is in the loop's value type,
/// through which the collector traces it; the typed factory keeps a numeric tape compact.
inline ::Array *newCollectionArray(const camel::core::type::ArrayType *arrayType) {
    return ::Array::create(camel::core::mm::autoSpace(), arrayType, 0);
}

} // namespace detail

/// Makes the first state of a loop, through `host` (initialState, setStateValue): the value when
/// k = 1 without collection, a k-tuple if not, and, when the loop collects, the tuple of the state
/// and one empty array per collection slot.
template <typename Host> inline void startLoopState(const LoopShape &shape, Host &host) {
    const size_t k = shape.stateCount;
    if (!shape.collecting()) {
        if (k == 1) {
            host.setStateValue(host.initialState(0));
        } else {
            ::Tuple *state = ::Tuple::create(k, camel::core::mm::autoSpace());
            for (size_t j = 0; j < k; ++j) {
                state->set(j, host.initialState(j), shape.valueType);
            }
            host.setStateValue(camel::core::rtdata::toSlot(state));
        }
        return;
    }
    // A collecting loop's state is (state_0 ... state_{k-1}, tape_0 ... tape_{s-1}) and stays in
    // that one tuple: the tapes are appended to, never copied.
    ASSERT(
        shape.valueType != nullptr && shape.valueType->size() == k + shape.collectionCount,
        "A collecting loop's value type does not have its k + s components.");
    ::Tuple *work = ::Tuple::create(k + shape.collectionCount, camel::core::mm::autoSpace());
    for (size_t j = 0; j < k; ++j) {
        work->set(j, host.initialState(j), shape.valueType);
    }
    for (size_t c = 0; c < shape.collectionCount; ++c) {
        const auto *arrayType =
            static_cast<const camel::core::type::ArrayType *>(shape.valueType->typeAt(k + c));
        work->set(k + c, detail::newCollectionArray(arrayType), shape.valueType);
    }
    host.setStateValue(camel::core::rtdata::toSlot(work));
}

/// Takes `result`, what the body returned for a round, as the next state (and appends the values
/// it collected to the tapes).
template <typename Host>
inline void takeLoopRound(const LoopShape &shape, Host &host, slot_t result) {
    if (!shape.collecting()) {
        host.setStateValue(result);
        return;
    }
    const size_t k = shape.stateCount;
    // Growing a tape is the loop's allocation. A memory exhaustion anywhere in the rounds is
    // reported at the loop rather than as a bare pass failure: the tapes are the only values a
    // collecting loop keeps across rounds, so the loop is what outgrew the limit (the GC may
    // notice it at a safepoint between rounds, not at the append that made it grow).
    try {
        const auto *produced = camel::core::rtdata::fromSlot<::Tuple *>(result);
        auto *state          = camel::core::rtdata::fromSlot<::Tuple *>(host.stateValue());
        for (size_t j = 0; j < k; ++j) {
            state->set(j, produced->get<slot_t>(j), shape.valueType);
        }
        for (size_t c = 0; c < shape.collectionCount; ++c) {
            const auto *arrayType =
                static_cast<const camel::core::type::ArrayType *>(shape.valueType->typeAt(k + c));
            auto *array = state->template get<::Array *>(k + c);
            array->append(produced->get<slot_t>(k + c), arrayType);
        }
    } catch (const camel::core::mm::MemoryExhausted &e) {
        throw e.during("collecting the values a loop produces each round", shape.origin);
    }
}

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
 *   const type::Type *elementType()           the declared type of the round's element port (what
 *                                             the round hand over must have, loopRoundValue)
 */
template <typename Host> inline void driveLoopRounds(const LoopShape &shape, Host &host) {
    startLoopState(shape, host);
    try {
        for (size_t round = 0;; ++round) {
            host.beforeRound(round);
            if (shape.cond != nullptr &&
                !camel::core::rtdata::fromSlot<bool>(
                    host.callRound(shape, shape.cond, nullptr))) {
                break;
            }
            slot_t element = NullSlot;
            if (shape.iterates) {
                ::Array *array = host.iterable();
                if (round >= array->size()) {
                    break;
                }
                // The round's value, along the first dimension and in the form its declared type
                // says (loopRoundValue).
                element = loopRoundValue(array, round, host.elementType());
            }
            takeLoopRound(
                shape,
                host,
                host.callRound(shape, shape.body, shape.iterates ? &element : nullptr));
        }
    } catch (const camel::core::mm::MemoryExhausted &e) {
        if (!shape.collecting()) {
            throw;
        }
        throw e.during("collecting the values a loop produces each round", shape.origin);
    }
}

} // namespace camel::execute