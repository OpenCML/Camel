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
 * Created: Sep. 16, 2025
 * Updated: Sep. 27, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "frame.h"
#include "compile/gir.h"

using namespace GraphIR;

Frame::Frame(const frame_ptr_t &parent, const graph_ptr_t &graph)
    : parent_(parent), graph_(graph), arena_(*graph->arena()) {
    EXEC_WHEN_DEBUG(l.in("Frame").debug(
        "Created Frame for Graph: {} (Parent: {})",
        graph_->name().empty() ? "<anonymous>" : graph_->name(),
        parent_ ? "Yes" : "No"));
}

Frame::~Frame() {
    EXEC_WHEN_DEBUG(l.in("Frame").debug(
        "Destroyed Frame for Graph: {} (Parent: {})",
        graph_->name().empty() ? "<anonymous>" : graph_->name(),
        parent_ ? "Yes" : "No"));
}

frame_ptr_t Frame::create(const frame_ptr_t &parent, const graph_ptr_t &graph) {
    return std::make_shared<Frame>(parent, graph);
}

data_ptr_t Frame::get(const node_ptr_t &node) {
    const graph_ptr_t &g = node->graph();
    if (g != graph_) {
        ASSERT(parent_, "Node does not belong to the current frame's graph.");
        return parent_->get(node);
    }
    ASSERT(
        arena_.has(node->index()),
        std::format(
            "Accessing non-exist data of node {} in arena: {}",
            node->toString(),
            arena_.toString()));
    return arena_.get(node->index());
}

void Frame::set(const node_ptr_t &node, const data_ptr_t &data) {
    const graph_ptr_t &g = node->graph();
    if (g != graph_) {
        ASSERT(parent_, "Node does not belong to the current frame's graph.");
        parent_->set(node, data);
        return;
    }
    arena_.set(node->index(), data);
}

frame_ptr_t Frame::push(const GraphIR::graph_ptr_t &graph) {
    ASSERT(graph, "Cannot push a null graph.");
    ASSERT(graph != graph_, "Cannot push the same graph as the current frame's graph.");
    return Frame::create(shared_from_this(), graph);
}

frame_ptr_t Frame::pop() {
    auto p = parent_;
    parent_ = nullptr;
    return p;
}

std::string Frame::toString() {
    return std::format(
        "Frame(Graph: {}, Parent: {})): {}",
        graph_->name().empty() ? "<anonymous>" : graph_->name(),
        parent_ ? "Yes" : "No",
        arena_.toString());
}
