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
 * Updated: Dec. 06, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "utils/log.h"

class Frame;
using frame_rptr_t = Frame *;

namespace GraphIR {
class Graph;
using data_idx_t = int16_t;
} // namespace GraphIR

class Frame {
  public:
    Frame(
        const TupleTypeLayout &layout, GraphIR::Graph *graph, GCTuple *staticTuple,
        IAllocator &allocator = mm::autoSpace())
        : graph_(graph), allocator_(&allocator), staticArea_(staticTuple) {
        ASSERT(graph_ != nullptr, "Frame graph is null.");
        ASSERT(staticArea_ != nullptr, "Static tuple is null.");

        // 初始化运行时区（动态区） Tuple
        dynamicArea_ = GCTuple::create(layout, graph_->argsCount(), allocator);
    }

    ~Frame() {
        if (dynamicArea_) {
            // dynamicArea_->~GCTuple();
            allocator_->free(dynamicArea_);
            dynamicArea_ = nullptr;
        }
        // staticArea_ 由外部管理，不释放。
    }

    GraphIR::Graph *graph() { return graph_; }
    const GraphIR::Graph *graph() const { return graph_; }

    template <typename T> T *get(GraphIR::data_idx_t index) {
        ASSERT(index != 0, "Data index is invalid.");
        if (index < 0) {
            // 静态区：index 从 -1, -2, ... 开始
            size_t idx = static_cast<size_t>(-index - 1);
            ASSERT(staticArea_ != nullptr, "Static tuple is null.");
            ASSERT(idx < staticArea_->size(), "Invalid static data index");
            return &staticArea_->at<T>(idx);
        } else {
            // 动态区：index 从 1 开始
            size_t idx = static_cast<size_t>(index - 1);
            ASSERT(idx < dynamicArea_->size(), "Invalid argument index");
            return &dynamicArea_->at<T>(idx);
        }
    }

    template <typename T> void set(GraphIR::data_idx_t index, T &&value) {
        ASSERT(index != 0, "Data index is invalid.");
        if (index < 0) {
            size_t idx = static_cast<size_t>(-index - 1);
            ASSERT(staticArea_ != nullptr, "Static tuple is null.");
            ASSERT(idx < staticArea_->size(), "Invalid static data index");
            staticArea_->at<T>(idx) = std::forward<T>(value);
        } else {
            size_t idx = static_cast<size_t>(index - 1);
            ASSERT(idx < dynamicArea_->size(), "Invalid argument index");
            dynamicArea_->at<T>(idx) = std::forward<T>(value);
        }
    }

  private:
    GraphIR::Graph *graph_ = nullptr;
    IAllocator *allocator_ = nullptr;

    /// 静态区：外部传入
    GCTuple *staticArea_ = nullptr;

    /// 动态区：本Frame生成并管理的
    GCTuple *dynamicArea_ = nullptr;
};
