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
 * Author: Zhenjie Wei
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The identity of a graph (docs/design/graph-store, 4.4).
 *
 * A GraphId names a graph for as long as the program lives: every version of a graph, from the
 * one the front end builds to the ones the commits of rewrites produce, carries the GraphId of
 * the graph it is a version of, and a graph that a rewrite creates gets a new one. Ids are not
 * reused. They are allocated per program (Context::graphIds), so two ids are comparable only
 * within one program.
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace camel::runtime {

enum class GraphId : uint32_t { Invalid = 0 };

inline constexpr GraphId kInvalidGraphId = GraphId::Invalid;

constexpr uint32_t toValue(GraphId id) { return static_cast<uint32_t>(id); }
constexpr bool isValid(GraphId id) { return id != GraphId::Invalid; }

/// Hands out the GraphIds of one program: 1, 2, 3, ... in the order of the requests. May be
/// called from any thread.
class GraphIdAllocator {
  public:
    GraphId allocate() {
        return static_cast<GraphId>(next_.fetch_add(1, std::memory_order_relaxed));
    }
    /// The number of ids handed out so far; every id is at most this.
    uint32_t count() const { return next_.load(std::memory_order_relaxed) - 1; }

  private:
    std::atomic<uint32_t> next_{1};
};

} // namespace camel::runtime
