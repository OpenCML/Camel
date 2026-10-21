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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * A loop whose condition and body are regions of its graph, run in the frame of the graph by an
 * interpreter that walks the execution plan (ExecPlan::loops, docs/technical/39_loops.md). The
 * interpreter calls stepLoops at each position of the plan before it runs the node there; the
 * function starts a loop, tests a round (stops the loop or binds the ports of the body) and ends
 * a round (takes the result of the body as the state and sends the interpreter back to the start
 * of the round), with the rules of loop_driver.h.
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/execute/loop_driver.h"
#include "camel/runtime/exec_plan.h"

#include <cstddef>
#include <cstdint>

namespace camel::execute {

namespace detail {

/// The services the state of a loop needs from the frame the loop runs in.
struct LoopFrameHost {
    camel::core::context::Frame &frame;
    const camel::runtime::ExecPlan &plan;
    const camel::runtime::ExecLoop &loop;

    slot_t initialState(size_t j) {
        return frame.get<slot_t>(plan.loopSlots[loop.slotsOffset + j]);
    }
    slot_t stateValue() { return frame.get<slot_t>(loop.stateSlot); }
    void setStateValue(slot_t value) { frame.set(loop.stateSlot, value); }
};

inline LoopShape loopShapeOf(const camel::runtime::ExecLoop &loop) {
    return LoopShape{
        .iterates        = loop.iterates,
        .stateCount      = loop.stateCount,
        .collectionCount = loop.collectionCount,
        .valueType       = loop.valueType,
        .origin          = loop.origin,
    };
}

/// Gives the ports at loopSlots[first ..< first + k] (from the loop's offset) the values of the
/// state.
inline void bindLoopState(
    camel::core::context::Frame &frame, const camel::runtime::ExecPlan &plan,
    const camel::runtime::ExecLoop &loop, size_t first) {
    const LoopShape shape = loopShapeOf(loop);
    const slot_t state    = frame.get<slot_t>(loop.stateSlot);
    for (size_t j = 0; j < loop.stateCount; ++j) {
        frame.set(
            plan.loopSlots[loop.slotsOffset + first + j],
            loopStateComponent(shape, state, j));
    }
}

} // namespace detail

/// What stepLoops asks the interpreter to do.
inline constexpr size_t kNoLoopJump = SIZE_MAX;
/// The loop whose round the interpreter resumes at the start of: its `Begin` is not run again.
inline constexpr uint32_t kNoLoop = UINT32_MAX;

/**
 * Runs the events of the loops of `plan` at topo index `at`, in `frame`. Returns the topo index
 * the interpreter goes on at, or kNoLoopJump to run the node at `at`. When a loop ends a round
 * and the interpreter goes back to the start of it, `resumed` is that loop and the interpreter
 * passes it back for the first position it runs: the loop is not started again.
 * `roundDone(loop, rounds)` is called when a round ended and the next starts (the interpreter's
 * safepoint and limits).
 */
template <typename RoundDone>
inline size_t stepLoops(
    const camel::runtime::ExecPlan &plan, size_t at, camel::core::context::Frame &frame,
    uint32_t &resumed, RoundDone &&roundDone) {
    using camel::runtime::ExecLoopEvent;
    using camel::core::rtdata::fromSlot;
    using camel::core::rtdata::toSlot;
    const uint32_t resumedHere = resumed;
    resumed                    = kNoLoop;
    for (const uint32_t code : plan.loopEventsAt(at)) {
        const uint32_t index                 = code / 3;
        const camel::runtime::ExecLoop &loop = plan.loops[index];
        const size_t k                       = loop.stateCount;
        detail::LoopFrameHost host{frame, plan, loop};
        switch (static_cast<ExecLoopEvent>(code % 3)) {
        case ExecLoopEvent::Begin:
            if (index != resumedHere) {
                frame.set(loop.roundSlot, toSlot<int64_t>(0));
                startLoopState(detail::loopShapeOf(loop), host);
                if (loop.hasCond) {
                    detail::bindLoopState(frame, plan, loop, k);
                }
            }
            break;
        case ExecLoopEvent::Test: {
            const int64_t round = fromSlot<int64_t>(frame.get<slot_t>(loop.roundSlot));
            bool stop           = false;
            slot_t element      = NullSlot;
            if (loop.hasCond) {
                stop = !fromSlot<bool>(frame.get<slot_t>(loop.condResultSlot));
            }
            if (!stop && loop.iterates) {
                const ::Array *array = frame.get<::Array *>(loop.iterableSlot);
                if (static_cast<size_t>(round) >= array->size()) {
                    stop = true;
                } else {
                    element = array->getSlot(static_cast<size_t>(round));
                }
            }
            if (stop) {
                frame.set(loop.roundSlot, toSlot<int64_t>(-1));
                if (loop.position != at) {
                    return loop.position;
                }
                break;
            }
            const size_t first = k + (loop.hasCond ? k : 0);
            if (loop.iterates) {
                frame.set(plan.loopSlots[loop.slotsOffset + first], element);
            }
            detail::bindLoopState(frame, plan, loop, first + (loop.iterates ? 1 : 0));
        } break;
        case ExecLoopEvent::End: {
            const int64_t round = fromSlot<int64_t>(frame.get<slot_t>(loop.roundSlot));
            if (round < 0) {
                break; // the test stopped the loop; its state is the value
            }
            takeLoopRound(
                detail::loopShapeOf(loop),
                host,
                frame.get<slot_t>(loop.bodyResultSlot));
            frame.set(loop.roundSlot, toSlot<int64_t>(round + 1));
            roundDone(loop, static_cast<size_t>(round + 1));
            if (loop.hasCond) {
                detail::bindLoopState(frame, plan, loop, k);
            }
            resumed = index;
            return loop.begin;
        }
        }
    }
    return kNoLoopJump;
}

} // namespace camel::execute
