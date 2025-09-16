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
 * Updated: Sep. 16, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "arena.h"
#include "compile/gir.h"
#include "utils/log.h"

class Frame;
using frame_ptr_t = std::shared_ptr<Frame>;

class Frame {
  public:
    Frame(const frame_ptr_t &parent, const GIR::graph_ptr_t &graph)
        : parent_(parent), graph_(graph), arena_(*graph->arena()) {
        l.in("Frame").debug(
            "Created Frame for Graph: {} (Parent: {})",
            graph_->name().empty() ? "<anonymous>" : graph_->name(),
            parent_ ? "Yes" : "No");
    }

    ~Frame() {
        l.in("Frame").debug(
            "Destroyed Frame for Graph: {} (Parent: {})",
            graph_->name().empty() ? "<anonymous>" : graph_->name(),
            parent_ ? "Yes" : "No");
    }

    static frame_ptr_t create(const frame_ptr_t &parent, const GIR::graph_ptr_t &graph) {
        return std::make_shared<Frame>(parent, graph);
    }

    DataArena &arena() { return arena_; }

    data_ptr_t get(const GIR::node_ptr_t &node) {
        const GIR::graph_ptr_t &g = node->graph();
        if (g.get() != graph_.get()) {
            ASSERT(parent_, "Node does not belong to the current frame's graph.");
            return parent_->get(node);
        }
        ASSERT(arena_.has(node->index()), "Data not found in frame's arena.");
        return arena_.get(node->index());
    }

    void set(const GIR::node_ptr_t &node, const data_ptr_t &data) {
        const GIR::graph_ptr_t &g = node->graph();
        if (g.get() != graph_.get()) {
            ASSERT(parent_, "Node does not belong to the current frame's graph.");
            parent_->set(node, data);
            return;
        }
        arena_.set(node->index(), data);
    }

    void detach() { parent_ = nullptr; }

  private:
    frame_ptr_t parent_;
    GIR::graph_ptr_t graph_;
    DataArena arena_;
};