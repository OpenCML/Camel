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
 * Updated: Dec. 07, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/rtdata/data.h"
#include "utils/brpred.h"
#include "utils/log.h"

class Frame;
using frame_rptr_t = Frame *;

namespace GraphIR {
class Graph;
using data_idx_t = int16_t;
} // namespace GraphIR

class FrameTemplate {
  public:
    FrameTemplate(GraphIR::Graph *graph, IAllocator &staticAllocator, IAllocator &dynamicAllocator);

    GraphIR::Graph *graph() const { return graph_; }

    GCTuple *staticArea() const { return staticArea_; }
    GCTuple *makeDynamicArea() const;

    IAllocator &staticAllocator() const { return staticAllocator_; }
    IAllocator &dynamicAllocator() const { return dynamicAllocator_; }

  private:
    GraphIR::Graph *graph_;
    IAllocator &staticAllocator_;
    IAllocator &dynamicAllocator_;
    TupleTypeLayout dynamicDataLayout_;
    GCTuple *staticArea_ = nullptr;
};

class Frame {
  public:
    Frame(const FrameTemplate &layout)
        : graph_(layout.graph()), allocator_(&layout.dynamicAllocator()),
          staticArea_(layout.staticArea()) {
        ASSERT(graph_ != nullptr, "Frame graph is null.");
        ASSERT(staticArea_ != nullptr, "Static tuple is null.");

        // 初始化运行时区（动态区） Tuple
        dynamicArea_ = layout.makeDynamicArea();
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

    template <typename T> T get(GraphIR::data_idx_t index) {
        ASSERT(index != 0, "Data index is invalid.");
        if (LIKELY(index > 0)) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(idx < dynamicArea_->size(), "Invalid argument index");
            return dynamicArea_->get<T>(idx);
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(idx < staticArea_->size(), "Invalid static data index");
            return staticArea_->get<T>(idx);
        }
    }

    template <typename T> void set(GraphIR::data_idx_t index, T value) {
        ASSERT(index != 0, "Data index is invalid.");
        if (index < 0) {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(idx < staticArea_->size(), "Invalid static data index");
            staticArea_->set<T>(idx, value);
        } else {
            size_t idx = static_cast<size_t>(index);
            ASSERT(idx < dynamicArea_->size(), "Invalid argument index");
            dynamicArea_->set<T>(idx, value);
        }
    }

  private:
    GraphIR::Graph *graph_ = nullptr;
    IAllocator *allocator_ = nullptr;
    GCTuple *staticArea_   = nullptr;
    GCTuple *dynamicArea_  = nullptr;
};
