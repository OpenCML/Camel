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
 * Updated: Nov. 02, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "arena.h"
#include "utils/log.h"

class Frame;
using frame_rptr_t = Frame *;

namespace GraphIR {
class Node;
class Graph;
using data_idx_t = int16_t;
using arr_size_t = uint16_t;
using node_ptr_t = std::shared_ptr<Node>;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace GraphIR

class FrameTemplate {};

class Frame {
  public:
    Frame() = default;
    Frame(GraphIR::Graph *graph);
    ~Frame() = default;

    const GraphIR::Graph *graph() const { return graph_; }

    Frame &operator=(const Frame &&other) {
        graph_ = other.graph_;
        dataArr_ = std::move(other.dataArr_);
        return *this;
    }

    bool has(const GraphIR::data_idx_t &index);
    data_ptr_t get(const GraphIR::data_idx_t &index);
    void set(const GraphIR::data_idx_t &index, const data_ptr_t &data);

    std::string toString() const;

  private:
    GraphIR::Graph *graph_;
    data_vec_t dataArr_;
};
