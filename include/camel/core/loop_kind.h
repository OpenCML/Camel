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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The three forms of a loop (design principles 3.10; docs/design/graph-model 3.5,
 * the LOOP card). The front end's tree (GCT), the graph's LOOP node and the consumer API
 * (camel/runtime/loop.h, LoopView) share this enumeration; its numeric values are stored in
 * frozen graphs and must not be renumbered.
 *
 *   While     loop s = s0 while c do b          a condition graph and a body graph
 *   For       loop s = s0 for x in xs do b      a body graph; the array is Norm input 0
 *   ForWhile  loop s = s0 for x in xs while c do b
 *             both: each round first evaluates the condition, then takes the next element;
 *             the loop ends when the condition is false or no element is left.
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
