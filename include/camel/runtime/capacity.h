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
 * Created: Oct. 07, 2026
 * Updated: Oct. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The capacity of one runtime graph.
 *
 * A GCGraph addresses its nodes, its edges (input table entries) and its payload words with 16
 * bits (gc_node_ref_t, gc_off_t, gc_cnt_t in graph.h). A graph that does not fit is not a
 * malformed graph and not a defect of the pass that built it: the function, after what a pass
 * inlined or unrolled into it, is larger than one graph can be. It is reported as such, with
 * the graph, the quantity, the size reached and the limit (RuntimeDiag::GraphTooLarge).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

namespace camel::runtime {

class GraphCapacityError : public std::runtime_error {
  public:
    /// `quantity` is a plural noun phrase ("nodes"); `size` is what the graph
    /// needs, `limit` the most a graph holds.
    GraphCapacityError(
        std::string quantity, size_t size, size_t limit, std::string graph = {},
        uint64_t origin = 0)
        : std::runtime_error(describe(quantity, size, limit, graph)),
          quantity_(std::move(quantity)), size_(size), limit_(limit), graph_(std::move(graph)),
          origin_(origin) {}

    const std::string &quantity() const { return quantity_; }
    size_t size() const { return size_; }
    size_t limit() const { return limit_; }
    /// Name of the graph, empty until someone who knows it says so (inGraph).
    const std::string &graph() const { return graph_; }
    /// Source origin of the graph (its definition, or one of its nodes), 0 when unknown.
    uint64_t origin() const { return origin_; }

    /// The same error about the graph `name`. The place that seals a draft knows the graph;
    /// the place that measures it does not. A name and an origin already set are kept.
    GraphCapacityError inGraph(const std::string &name, uint64_t origin) const {
        return GraphCapacityError(
            quantity_,
            size_,
            limit_,
            graph_.empty() ? name : graph_,
            origin_ != 0 ? origin_ : origin);
    }

  private:
    static std::string
    describe(const std::string &quantity, size_t size, size_t limit, const std::string &graph) {
        return std::format(
            "{} needs {} {}, and one runtime graph holds at most {} (they are addressed with 16 "
            "bits)",
            graph.empty() ? std::string("A graph") : std::format("Graph '{}'", graph),
            size,
            quantity,
            limit);
    }

    std::string quantity_;
    size_t size_;
    size_t limit_;
    std::string graph_;
    uint64_t origin_;
};

} // namespace camel::runtime
