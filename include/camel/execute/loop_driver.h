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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The rounds of a LOOP node, once for every interpreter (docs/design/loops, docs/technical/
 * 39_loops.md): the order of a round, the layout of the ports a round binds, and what ends the
 * loop are the loop's (design principles 2.14, camel/core/loop_kind.h), not an interpreter's.
 * NodeVM, FastVM's native loop and the macro evaluator each supply only the services below, so a
 * change of the rules (more things to iterate over than arrays) is made here.
 *
 * Each round: the condition first (a `while` or a `for … while`), then the next element (a `for`),
 * then the body, whose result is the next state. The state is the value (k = 1) or a k-tuple.
 *
 * The header-only template keeps the loop as fast as the three copies were: the services are
 * inlined.
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/rtdata/array.h"
#include "camel/runtime/graph.h"

#include <cstddef>

namespace camel::execute {

/// What a loop does, as the interpreters need it.
struct LoopShape {
    camel::runtime::GCGraph *cond = nullptr; // while, for … while; nullptr for `for`
    camel::runtime::GCGraph *body = nullptr;
    bool iterates                 = false; // for, for … while: one element a round
    size_t stateCount             = 1;     // k
};

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
 * Runs the rounds of a loop whose state `host` already holds. The host provides:
 *
 *   void beforeRound(size_t round)            a round is about to start (a limit, a safepoint)
 *   slot_t callRound(GCGraph *target, const slot_t *element)
 *                                             one call of the condition or of the body for this
 *                                             round: the interpreter's own frame, recursion check
 *                                             and call, with the ports bound (bindLoopRound);
 *                                             returns the graph's result
 *   Array *iterable()                         the array a `for` iterates, read at each round
 *   void advance(slot_t next)                 the body's result becomes the state
 */
template <typename Host> inline void driveLoopRounds(const LoopShape &shape, Host &host) {
    for (size_t round = 0;; ++round) {
        host.beforeRound(round);
        // Each round: the condition first, then the next element (camel/core/loop_kind.h).
        if (shape.cond != nullptr &&
            !camel::core::rtdata::fromSlot<bool>(host.callRound(shape.cond, nullptr))) {
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
        host.advance(host.callRound(shape.body, shape.iterates ? &element : nullptr));
    }
}

} // namespace camel::execute
