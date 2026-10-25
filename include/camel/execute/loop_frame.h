// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include "camel/profile/profile.h"
#include "camel/runtime/exec_plan.h"
#include "camel/utils/brpred.h"

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
        //  round.
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

/// The j-th value of the running state: the loop's tuple, or — when the plan keeps the state as
/// its components — the body slot the component is computed in (ExecLoop::stateComponents).
CAMEL_ALWAYS_INLINE slot_t loopStateComponent(
    const camel::runtime::ExecLoop &loop, camel::core::context::Frame &frame, size_t j) {
    if (loop.stateComponents) {
        return frame.get<slot_t>(loop.stateSlots[j]);
    }
    return loopStateComponent(loopShapeOf(loop), frame.get<slot_t>(loop.stateSlot), j);
}

/// Gives the ports at loop.slots[first ..< first + k] the values of the state.
///
/// This runs twice a round (the ports of the body after the test, those of the condition after
/// the body), so the shapes a loop actually has are spelled out instead of a loop over the
/// components, each of them through LoopShape: the components of a multi-value state
/// (ExecLoop::stateComponents), and the one value of state that is the loop's value itself (k = 1,
/// nothing collected). Every index is still read through Frame::get/Frame::set, which is what says
/// what a slot of the static area is.
CAMEL_ALWAYS_INLINE void bindLoopState(
    camel::core::context::Frame &frame, const camel::runtime::ExecLoop &loop, size_t first) {
    const uint32_t count = loop.stateCount;
    if (loop.stateComponents) [[likely]] {
        const camel::runtime::gc_data_idx_t *source = loop.stateSlots;
        const camel::runtime::gc_data_idx_t *target = loop.slots + first;
        switch (count) {
        case 1: {
            const slot_t value = frame.get<slot_t>(source[0]);
            frame.set(target[0], value);
            return;
        }
        case 2: {
            const slot_t firstValue  = frame.get<slot_t>(source[0]);
            const slot_t secondValue = frame.get<slot_t>(source[1]);
            frame.set(target[0], firstValue);
            frame.set(target[1], secondValue);
            return;
        }
        default:
            break;
        }
    } else if (count == 1 && loop.collectionCount == 0) [[likely]] {
        // One value of state and nothing collected: the state is that value, not a tuple around it
        // (LoopShape::valueIsTuple), and the port takes it as it is.
        const slot_t value = frame.get<slot_t>(loop.stateSlot);
        frame.set(loop.slots[first], value);
        return;
    }
    for (size_t j = 0; j < count; ++j) {
        frame.set(loop.slots[first + j], loopStateComponent(loop, frame, j));
    }
}

} // namespace detail

/// The loop starts: the round count is 0, the state is the first one, the ports of the condition
/// have it.
inline void loopBegin(camel::core::context::Frame &frame, const camel::runtime::ExecLoop &loop) {
    detail::LoopFrameHost host{frame, loop};
    frame.set(loop.roundSlot, camel::core::rtdata::toSlot(int64_t{0}));
    startLoopState(detail::loopShapeOf(loop), host);
    if (loop.stateComponents) {
        // The loop's value tuple was made from the first state, as a loop of one value makes its
        // value; the rounds then work on the components in the body's own slots. The tuple stays
        // in the state slot — it is the loop's value, and what the collector traces — and is
        // brought up to date when the loop ends (loopEnd).
        const slot_t state = frame.get<slot_t>(loop.stateSlot);
        for (size_t j = 0; j < loop.stateCount; ++j) {
            frame.set(
                loop.stateSlots[j],
                camel::core::rtdata::materializeCallResult(
                    loopStateComponent(detail::loopShapeOf(loop), state, j),
                    frame.typeAt<camel::core::type::Type>(loop.stateSlots[j])));
        }
    }
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
        if (loop.intervalIterates) {
            // The sequence is the interval [from, to) and no array of it exists: the round takes
            // `from + round` while the round number is below the number of elements
            // (`for i in a..b`: the compiler fuses the interval, design principles 4.2). The
            // count is the difference of two 64-bit integers taken unsigned, so an interval of
            // any two integers has a well-defined number of elements; the element itself cannot
            // overflow, because it is below `to` when the round is below that count.
            const int64_t from = fromSlot<int64_t>(frame.get<slot_t>(loop.intervalFromSlot));
            const int64_t to   = fromSlot<int64_t>(frame.get<slot_t>(loop.intervalToSlot));
            const uint64_t elements =
                to > from ? static_cast<uint64_t>(to) - static_cast<uint64_t>(from) : uint64_t{0};
            if (static_cast<uint64_t>(round) >= elements) {
                stop = true;
            } else {
                element = toSlot(from + static_cast<int64_t>(round));
            }
        } else {
            const ::Array *array = frame.get<::Array *>(loop.iterableSlot);
            if (static_cast<size_t>(round) >= array->size()) {
                stop = true;
            } else {
                element = array->getSlot(static_cast<size_t>(round));
            }
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
        if (loop.stateComponents) {
            // The body's tuple was not made (the plan elides it): the loop's value is the tuple
            // made when the loop started, brought up to date from the components the last round
            // left. It is the loop's own value, so it is written where it is.
            const auto shape = detail::loopShapeOf(loop);
            auto *value      = fromSlot<::Tuple *>(frame.get<slot_t>(loop.stateSlot));
            ASSERT(value != nullptr, "A loop of components has its value tuple.");
            for (size_t j = 0; j < loop.stateCount; ++j) {
                value->set(j, frame.get<slot_t>(loop.stateSlots[j]), shape.valueType);
            }
        }
        return false;
    }
    if (!loop.stateComponents) {
        detail::LoopFrameHost host{frame, loop};
        if (loop.collectionCount == 0) [[likely]] {
            // A loop that collects nothing: the next state is what the body returned, and
            // takeLoopRound would only have done this (loop_driver.h). Spelled out here because
            // the call is measurable a round in the shapes that do not keep the state as its
            // components (one value of state, or a tuple the plan could not elide).
            host.setStateValue(frame.get<slot_t>(loop.bodyResultSlot));
        } else {
            takeLoopRound(detail::loopShapeOf(loop), host, frame.get<slot_t>(loop.bodyResultSlot));
        }
    }
    frame.set(loop.roundSlot, toSlot(round + 1));
    roundDone(loop, static_cast<size_t>(round + 1));
    // One round of this loop ended: the step boundary and the per-step numbers of `std::profile`
    // (camel/profile/profile.h). A flag read and an unlikely branch when profiling is off, once
    // a round (enabledInline: a call out of the interpreter here is measurable).
    if (camel::profile::enabledInline()) [[unlikely]] {
        camel::profile::loopRound(loop.origin);
    }
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
        const auto event                     = static_cast<ExecLoopEvent>(code % 3);
        // Going back to the start of a round of a loop is not starting or testing the loops
        // around it, which are in the middle of a round; those inside it start again.
        if (resumedHere != kNoLoop && event != ExecLoopEvent::End &&
            loop.position > plan.loops[resumedHere].position) {
            continue;
        }
        switch (event) {
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
