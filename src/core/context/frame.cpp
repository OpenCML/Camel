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
 * Updated: Sep. 30, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "frame.h"
#include "compile/gir.h"

using namespace GraphIR;

Frame::Frame(const frame_ptr_t &parent, Graph &graph)
    : parent_(parent), graph_(graph), dataArr_(graph.runtimeDataSize(), nullptr) {
    EXEC_WHEN_DEBUG(l.in("Frame").debug(
        "Created Frame for Graph: {} (Parent: {})",
        graph_.name(),
        parent_ ? parent_->graph().name() : "none"));
}

Frame::~Frame() {
    EXEC_WHEN_DEBUG(l.in("Frame").debug(
        "Destroyed Frame for Graph: {} (Parent: {})",
        graph_.name(),
        parent_ ? parent_->graph().name() : "none"));
}

frame_ptr_t Frame::create(const frame_ptr_t &parent, Graph &graph) {
    return std::make_shared<Frame>(parent, graph);
}

data_ptr_t Frame::get(const node_ptr_t &node) {
    if (&node->graph() != &graph_) {
        ASSERT(parent_, "Node does not belong to the current frame's graph.");
        return parent_->get(node);
    }
    if (node->type() == NodeType::DATA) {
        return graph_.getStaticData(node->index());
    } else {
        ASSERT(node->index() < dataArr_.size(), "Data index out of range.");
        return dataArr_[node->index()];
    }
}

void Frame::set(const node_ptr_t &node, const data_ptr_t &data) {
    if (&node->graph() != &graph_) {
        ASSERT(parent_, "Node does not belong to the current frame's graph.");
        return parent_->set(node, data);
    }
    if (node->type() == NodeType::DATA) {
        return graph_.setStaticData(node->index(), data);
    } else {
        ASSERT(node->index() < dataArr_.size(), "Data index out of range.");
        dataArr_[node->index()] = data;
        return;
    }
}

frame_ptr_t Frame::push(Graph &graph) {
    ASSERT(&graph != &graph_, "Cannot push the same graph as the current frame's graph.");
    return Frame::create(shared_from_this(), graph);
}

frame_ptr_t Frame::pop() {
    auto p = parent_;
    parent_ = nullptr;
    return p;
}

std::string Frame::toString() const {
    std::ostringstream oss;

    auto printDataArr = [&](const std::vector<data_ptr_t> &arr) {
        oss << "[";
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) {
                oss << ", ";
            }
            if (arr[i]) {
                oss << std::string_view(arr[i]->toString());
            } else {
                oss << "none";
            }
        }
        oss << "]";
        return oss.str();
    };

    oss << std::format(
        "Frame(Graph: {}, Parent: {})): (static)[",
        graph_.name(),
        parent_ ? parent_->graph().name() : "none");

    printDataArr(graph_.staticDataArr());

    oss << std::format("] (runtime)[");

    printDataArr(dataArr_);

    oss << "]";

    return oss.str();
}
