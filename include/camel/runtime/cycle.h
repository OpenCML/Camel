// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * One judgement of whether a graph closes a cycle, for the draft an optimisation writes and for
 * the frozen graph a back end is handed.
 *
 * A cycle is not a rule that got a rewrite wrong: each rewrite is legal by its own rule, and a
 * cycle can be composed out of several of them (a merge hands one node's waits to another node's
 * users, a splice moves a body's chain into its caller). Nothing at the rule layer can see that,
 * so the judgement is made on the whole graph -- once for the draft, and again when the graph is
 * frozen. Both sides walk the edge kinds listed here, and only here: a sync edge is a real
 * ordering (docs/design/effects/edges.cn.md, section 1), so a graph that closes a cycle on any of
 * the kinds has no order its back ends could run, and a difference between the two lists would be
 * a graph one side accepts and the other refuses.
 */

#pragma once

#include "camel/runtime/node_ref.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace camel::runtime {

/// The kinds of edge that make one node run after another. `Order` is last: `findInputCycle`
/// walks kinds by increasing value up to kCycleEdgeKindCount, so a kind added here is walked by
/// both the draft's check and the freeze's without touching either.
enum class CycleEdgeKind : uint8_t { Norm = 0, With, Ctrl, Sync, Order };
inline constexpr size_t kCycleEdgeKindCount = static_cast<size_t>(CycleEdgeKind::Order) + 1;

/**
 * Walks every edge kind of `edges` depth first and returns the node that closes a cycle, or
 * kInvalidNodeRef when the graph has none. When `path` is not null it is filled with the nodes on
 * the cycle, from the closing node to the node the walk was at when it found it, so that a caller
 * can say which chain it found.
 *
 * `Edges` is an adaptor the walk reads: `slotCount()`, `present(ref)` and `inputsOf(ref, kind)`,
 * the last returning a span that stays valid until the next call. A draft reads a GraphDraft
 * (src/passes/opt/generic/rewrite.cpp); a frozen graph reads a GCGraph, whose order edges are
 * records rather than a run of refs (src/runtime/graph_edges.cpp).
 */
template <class Edges>
gc_node_ref_t findInputCycle(Edges &edges, std::vector<gc_node_ref_t> *path = nullptr) {
    const size_t slots = edges.slotCount();
    std::vector<uint8_t> state(slots, 0); // 0 unvisited, 1 on the stack, 2 finished
    struct Frame {
        gc_node_ref_t node;
        uint8_t kind;
        size_t next;
    };
    std::vector<Frame> stack;
    for (gc_node_ref_t start = 0; start < slots; ++start) {
        if (state[start] != 0 || !edges.present(start)) {
            continue;
        }
        stack.assign(1, Frame{start, 0, 0});
        state[start] = 1;
        while (!stack.empty()) {
            Frame &top = stack.back();
            std::span<const gc_node_ref_t> inputs;
            while (top.kind < kCycleEdgeKindCount) {
                inputs = edges.inputsOf(top.node, static_cast<CycleEdgeKind>(top.kind));
                if (top.next < inputs.size()) {
                    break;
                }
                ++top.kind;
                top.next = 0;
            }
            if (top.kind >= kCycleEdgeKindCount) {
                state[top.node] = 2;
                stack.pop_back();
                continue;
            }
            const gc_node_ref_t input = inputs[top.next++];
            if (input >= slots || !edges.present(input)) {
                continue;
            }
            if (state[input] == 1) {
                if (path != nullptr) {
                    path->clear();
                    bool on = false;
                    for (const Frame &frame : stack) {
                        on = on || frame.node == input;
                        if (on) {
                            path->push_back(frame.node);
                        }
                    }
                }
                return input;
            }
            if (state[input] == 0) {
                state[input] = 1;
                stack.push_back({input, 0, 0});
            }
        }
    }
    return kInvalidNodeRef;
}

/**
 * Rewrite sessions dropped because the graph they wrote closed a cycle.
 *
 * A dropped session is not a repair: the program keeps the graph it had, so one optimisation is
 * skipped and nothing else changes. Which is why the count has to be readable rather than the
 * drop staying invisible -- `std::stats` prints it as `cycle_dropped_sessions`, so a rule that
 * composes a cycle shows up in a reading instead of quietly doing less work. One counter for the
 * process: a run reports what it did.
 */
inline std::atomic<size_t> &cycleDroppedSessionsCounter() {
    static std::atomic<size_t> counter{0};
    return counter;
}

/// How many rewrite sessions this process has dropped for closing a cycle.
inline size_t cycleDroppedSessions() {
    return cycleDroppedSessionsCounter().load(std::memory_order_relaxed);
}

/// Called where a session is dropped: the rewrite's draft closed a cycle.
inline void recordCycleDroppedSession() {
    cycleDroppedSessionsCounter().fetch_add(1, std::memory_order_relaxed);
}

} // namespace camel::runtime