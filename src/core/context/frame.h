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
#include "utils/log.h"

class Frame;
using frame_ptr_t = std::shared_ptr<Frame>;

namespace GraphIR {
class Node;
class Graph;
using node_ptr_t = std::shared_ptr<Node>;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace GraphIR

class Frame {
  public:
    Frame(const frame_ptr_t &parent, const GraphIR::graph_ptr_t &graph);
    ~Frame();

    static frame_ptr_t create(const frame_ptr_t &parent, const GraphIR::graph_ptr_t &graph);

    DataArena &arena() { return arena_; }

    data_ptr_t get(const GraphIR::node_ptr_t &node);
    void set(const GraphIR::node_ptr_t &node, const data_ptr_t &data);
    void detach() { parent_ = nullptr; }

  private:
    frame_ptr_t parent_;
    GraphIR::graph_ptr_t graph_;
    DataArena arena_;
};