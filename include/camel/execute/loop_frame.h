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
 * three events of a loop are loopBegin (the loop starts: its first state), loopTest (the
 * condition, if any, has run: stop the loop or bind the ports of the body) and loopEnd (the body
 * has run: take its result as the state and go back to the start of the round, or leave), with
 * the rules of loop_driver.h. An interpreter that walks the nodes calls stepLoops at each
 * position of the plan before it runs the node there; the bytecode of FastVM has an instruction
 * for each event.
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/rtdata/aggregate.h"
#include "camel/execute/loop_driver.h"
#include "camel/runtime/exec_plan.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace camel::execute {

namespace detail {

/// The services the state of a loop needs from the frame the loop runs in.
struct LoopFrameHost {
    camel::core::context::Frame &frame;
    const camel::runtime::ExecLoop &loop;

    slot_t initialState(size_t j) { return frame.get<slot_t>(loop.slots[j]); }
    slot_t stateValue() { return frame.get<slot_t>(loop.stateSlot); }
    void setStateValue(slot_t value) {
        // The next round reads this value: it is a value, not a view of the element of this
        // round (H29).
        frame.set(
            loop.stateSlot,
            camel::core::rtdata::materializeCallResult(
                value,
                frame.typeAt<camel::core::type::Type>(loop.stateSlot)));
    }
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

/// Gives the ports at loop.slots[first ..< first + k] the values of the state.
inline void bindLoopState(
    camel::core::context::Frame &frame, const camel::runtime::ExecLoop &loop, size_t first) {
    const LoopShape shape = loopShapeOf(loop);
    const slot_t state    = frame.get<slot_t>(loop.stateSlot);
    for (size_t j = 0; j < loop.stateCount; ++j) {
        frame.set(loop.slots[first + j], loopStateComponent(shape, state, j));
    }
}

} // namespace detail

/// The loop starts: the round count is 0, the state is the first one, the ports of the condition
/// have it.
inline void loopBegin(camel::core::context::Frame &frame, const camel::runtime::ExecLoop &loop) {
    detail::LoopFrameHost host{frame, loop};
    frame.set(loop.roundSlot, camel::core::rtdata::toSlot(int64_t{0}));
    startLoopState(detail::loopShapeOf(loop), host);
    if (loop.hasCond) {
        detail::bindLoopState(frame, loop, loop.stateCount);
    }
}

/// The condition (if any) has run: true when the loop stops (the round count becomes -1),
/// else the ports of the body have the element and the state.
inline bool loopTest(camel::core::context::Frame &frame, const camel::runtime::ExecLoop &loop) {
    using camel::core::rtdata::fromSlot;
    using camel::core::rtdata::toSlot;
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
        frame.set(loop.roundSlot, toSlot(int64_t{-1}));
        return true;
    }
    const size_t k     = loop.stateCount;
    const size_t first = k + (loop.hasCond ? k : 0);
    if (loop.iterates) {
        frame.set(loop.slots[first], element);
    }
    detail::bindLoopState(frame, loop, first + (loop.iterates ? 1 : 0));
    return false;
}

/// The body has run: true when another round starts (`roundDone(loop, rounds)` is called first,
/// the interpreter's safepoint and limits), false when the test had stopped the loop; its state
/// is its value.
template <typename RoundDone>
inline bool loopEnd(
    camel::core::context::Frame &frame, const camel::runtime::ExecLoop &loop,
    RoundDone &&roundDone) {
    using camel::core::rtdata::fromSlot;
    using camel::core::rtdata::toSlot;
    const int64_t round = fromSlot<int64_t>(frame.get<slot_t>(loop.roundSlot));
    if (round < 0) {
        return false;
    }
    detail::LoopFrameHost host{frame, loop};
    takeLoopRound(detail::loopShapeOf(loop), host, frame.get<slot_t>(loop.bodyResultSlot));
    frame.set(loop.roundSlot, toSlot(round + 1));
    roundDone(loop, static_cast<size_t>(round + 1));
    if (loop.hasCond) {
        detail::bindLoopState(frame, loop, loop.stateCount);
    }
    return true;
}

/// The `roundDone` of an interpreter whose rounds end in `safepoint()`: a memory limit hit there by
/// a loop that collects is reported at the loop (loop_driver.h, takeLoopRound).
template <typename Safepoint> inline auto loopRoundSafepoint(Safepoint &&safepoint) {
    return [safepoint = std::forward<Safepoint>(safepoint)](
               const camel::runtime::ExecLoop &loop, size_t) {
        try {
            safepoint();
        } catch (const camel::core::mm::MemoryExhausted &e) {
            if (loop.collectionCount == 0) {
                throw;
            }
            throw e.during("collecting the values a loop produces each round", loop.origin);
        }
    };
}

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
    const uint32_t resumedHere = resumed;
    resumed                    = kNoLoop;
    for (const uint32_t code : plan.loopEventsAt(at)) {
        const uint32_t index                 = code / 3;
        const camel::runtime::ExecLoop &loop = plan.loops[index];
        switch (static_cast<ExecLoopEvent>(code % 3)) {
        case ExecLoopEvent::Begin:
            if (index != resumedHere) {
                loopBegin(frame, loop);
            }
            break;
        case ExecLoopEvent::Test:
            if (loopTest(frame, loop) && loop.position != at) {
                return loop.position;
            }
            break;
        case ExecLoopEvent::End:
            if (loopEnd(frame, loop, roundDone)) {
                resumed = index;
                return loop.begin;
            }
            break;
        }
    }
    return kNoLoopJump;
}

} // namespace camel::execute
