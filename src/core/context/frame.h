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
 * Updated: Dec. 10, 2025
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
    FrameTemplate() = delete;
    FrameTemplate(GraphIR::Graph *graph, IAllocator &staticAllocator, IAllocator &runtimeAllocator);

    GraphIR::Graph *graph() const { return graph_; }

    Tuple *staticArea() const { return staticArea_; }
    Tuple *makeDynamicArea() const;

    IAllocator &staticAllocator() const { return staticAllocator_; }
    IAllocator &runtimeAllocator() const { return runtimeAllocator_; }

  private:
    GraphIR::Graph *graph_;
    IAllocator &staticAllocator_;
    IAllocator &runtimeAllocator_;
    const TupleTypeLayout *runtimeDataLayout_;
    Tuple *staticArea_ = nullptr;
};

class Frame {
  public:
    Frame() = delete;
    Frame(const FrameTemplate &temp)
        : graph_(temp.graph()), allocator_(&temp.runtimeAllocator()),
          staticArea_(temp.staticArea()) {
        ASSERT(graph_ != nullptr, "Frame graph is null.");
        ASSERT(staticArea_ != nullptr, "Static tuple is null.");
        dynamicArea_ = temp.makeDynamicArea();
    }
    // 不允许拷贝但可移动
    Frame(const Frame &) = delete;
    Frame(Frame &&other) noexcept
        : graph_(other.graph_), allocator_(other.allocator_), staticArea_(other.staticArea_),
          dynamicArea_(other.dynamicArea_) {
        // 防止已被移动的对象意外释放数据
        other.graph_       = nullptr;
        other.allocator_   = nullptr;
        other.staticArea_  = nullptr;
        other.dynamicArea_ = nullptr;
    }

    Frame &operator=(const Frame &other) = delete;
    Frame &operator=(Frame &&other) noexcept {
        if (this != &other) {
            // 先释放自己的资源
            if (dynamicArea_) {
                allocator_->free(dynamicArea_);
            }
            // 转移资源
            graph_       = other.graph_;
            allocator_   = other.allocator_;
            staticArea_  = other.staticArea_;
            dynamicArea_ = other.dynamicArea_;
            // 防止已被移动的对象意外释放数据
            other.graph_       = nullptr;
            other.allocator_   = nullptr;
            other.staticArea_  = nullptr;
            other.dynamicArea_ = nullptr;
        }
        return *this;
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

    TypeCode typeAt(GraphIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        if (LIKELY(index > 0)) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(idx < dynamicArea_->size(), "Invalid argument index");
            return dynamicArea_->typeAt(idx);
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(idx < staticArea_->size(), "Invalid static data index");
            return staticArea_->typeAt(idx);
        }
    }

    template <typename T> std::shared_ptr<T> typePtrAt(GraphIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        if (LIKELY(index > 0)) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(idx < dynamicArea_->size(), "Invalid argument index");
            auto res = graph_->runtimeDataType()->typeAt(idx);
            ASSERT(res.has_value(), std::format("Type at index {} is null.", idx));
            return tt::as_shared<T>(res.value());
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(idx < staticArea_->size(), "Invalid static data index");
            auto res = graph_->staticDataType()->typeAt(idx);
            ASSERT(res.has_value(), std::format("Type at index {} is null.", idx));
            return tt::as_shared<T>(res.value());
        }
    }

    template <typename T> T get(GraphIR::data_idx_t index) {
        ASSERT(index != 0, "Data index is invalid.");
        T res;
        if (LIKELY(index > 0)) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(idx < dynamicArea_->size(), "Invalid argument index");
            res = dynamicArea_->get<T>(idx);
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(idx < staticArea_->size(), "Invalid static data index");
            res = staticArea_->get<T>(idx);
        }
        ASSERT(
            res != static_cast<T>(0),
            std::format("Retrieved null data from frame at index {}", index));
        return res;
    }

    template <typename T> void set(GraphIR::data_idx_t index, T value) {
        ASSERT(index != 0, "Data index is invalid.");
        ASSERT(
            value != static_cast<T>(0),
            std::format("Cannot set null data into frame at index {}", index));
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
    Tuple *staticArea_     = nullptr;
    Tuple *dynamicArea_    = nullptr;
};
