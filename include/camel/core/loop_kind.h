// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The three forms of a loop (design principles 3.10; docs/design/graph-model 3.5,
 * the LOOP card). The front end's tree (GCT), the graph's LOOP node and the consumer API
 * (camel/runtime/loop.h, LoopView) share this enumeration; its numeric values are stored in
 * frozen graphs and must not be renumbered.
 *
 *   While     loop s = s0 while c do b          a condition graph and a body graph
 *   For       loop s = s0 for x in xs do b      a body graph; the sequence is Norm input 0
 *   ForWhile  loop s = s0 for x in xs while c do b
 *             both: each round first evaluates the condition, then takes the next element;
 *             the loop ends when the condition is false or no element is left.
 *
 * The sequence of a loop that iterates is an array, or the two bounds of an interval
 * (LoopIterableForm): `for i in 0..n` needs no array built (see the enumeration below).
 */

#pragma once

#include <cstdint>

namespace camel::core {

enum class LoopKind : uint8_t {
    While    = 0,
    For      = 1,
    ForWhile = 2,
};

/// The loop iterates over an array (Norm input 0 of the node; the body's first parameter).
constexpr bool loopHasIterable(LoopKind kind) { return kind != LoopKind::While; }
/// The loop has a condition graph.
constexpr bool loopHasCondition(LoopKind kind) { return kind != LoopKind::For; }

/*
 * What the elements of a loop that iterates come from (design principles 4.2; the author's
 * ruling of 2026-10-24 on `h248-generator-and-composition`: "`for i in 0..n` is fused by the
 * compiler, without materialising an array"). Norm input 0 of the LOOP node is one value in
 * both forms, so the state's places do not move; only what the round takes the element from
 * does. Its numeric values are stored in frozen graphs and must not be renumbered.
 *
 *   Array     loop s = s0 for x in xs do b     the sequence is an array; the round takes
 *                                              its element at the index the round number is
 *   Interval  loop s = s0 for i in a..b do b   the sequence is the pair `(a, b)`, the two
 *                                              integers of the interval [a, b); no array of
 *                                              the elements is made, and the round takes
 *                                              `a + round` while `round < b - a`
 */
enum class LoopIterableForm : uint8_t {
    Array    = 0,
    Interval = 1,
};

/// "while", "for", "for-while": the word dumps and diagnostics use.
constexpr const char *loopKindName(LoopKind kind) {
    switch (kind) {
    case LoopKind::While:
        return "while";
    case LoopKind::For:
        return "for";
    case LoopKind::ForWhile:
        return "for-while";
    }
    return "?";
}

} // namespace camel::core
