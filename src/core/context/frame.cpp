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

inline std::string formatAddress(void *ptr) {
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);

    std::stringstream ss;
    ss << std::hex << std::uppercase << addr;
    std::string hexStr = ss.str();

    if (hexStr.length() < 16) {
        hexStr = std::string(16 - hexStr.length(), '0') + hexStr;
    }

    std::string formatted;
    for (size_t i = 0; i < hexStr.length(); ++i) {
        formatted += hexStr[i];
        if ((i + 1) % 4 == 0 && i + 1 != hexStr.length())
            formatted += '\'';
    }

    return "0x" + formatted;
}

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
    if (&(node->graph()) != &graph_) {
        EXEC_WHEN_DEBUG(l.in("Frame").debug(
            "Getting data for node {}::{} from parent ({}) of current frame {}. {} != {}",
            node->graph().name(),
            node->toString(),
            parent_ ? parent_->graph().name() : "none",
            graph_.name(),
            formatAddress(&(node->graph())),
            formatAddress(&graph_)));
        ASSERT(
            parent_,
            std::format(
                "Node {} of graph {} does not belong to the current frame's graph {}",
                node->toString(),
                node->graph().name(),
                graph_.name()));
        return parent_->get(node);
    }
    if (node->type() == NodeType::DATA) {
        return graph_.getStaticData(node->index());
    } else {
        ASSERT(node->index() < dataArr_.size(), "Data index out of range.");
        const data_ptr_t &res = dataArr_[node->index()];
        ASSERT(
            res != nullptr,
            std::format(
                "Accessing uninitialized data of node: {}::{}",
                graph_.name(),
                node->toString()));
        return res;
    }
}

void Frame::set(const node_ptr_t &node, const data_ptr_t &data) {
    if (&(node->graph()) != &graph_) {
        EXEC_WHEN_DEBUG(l.in("Frame").debug(
            "Setting data for node {}::{} from parent ({}) of current frame {}. {} != {}",
            node->graph().name(),
            node->toString(),
            parent_ ? parent_->graph().name() : "none",
            graph_.name(),
            formatAddress(&(node->graph())),
            formatAddress(&graph_)));
        ASSERT(
            parent_,
            std::format(
                "Node {} of graph {} does not belong to the current frame's graph {}",
                node->toString(),
                node->graph().name(),
                graph_.name()));
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
