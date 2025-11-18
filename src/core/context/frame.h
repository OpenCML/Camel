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
 * Updated: Nov. 16, 2025
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

class Frame : public GCObject {
  public:
    Frame(const CompositeDataLayout *layout, GraphIR::Graph *graph, IAllocator &allocator);
    ~Frame();

    template <typename T> T *get(GraphIR::data_idx_t index) {
        ASSERT(graph_ != nullptr, "Frame graph is null.");
        ASSERT(index != 0, "Data index is invalid.");
        EXEC_WHEN_DEBUG(
            l.in("Frame").debug("Getting data of graph {} with index {}", graph_->name(), index));

        if (index < 0) {
            return graph_->getStaticData(index);
        }

        size_t idx = static_cast<size_t>(index) - 1;
        ASSERT(idx < graph_->argsCount(), "Invalid argument index");
        ASSERT(
            data_[idx] != nullptr,
            std::format("Accessing uninitialized data: {}::{}", graph_->name(), idx));
        return reinterpret_cast<T *>(data_[idx]);
    }

    template <typename T> void set(GraphIR::data_idx_t index, T &&value) {
        ASSERT(graph_ != nullptr, "Frame graph is null.");
        ASSERT(index != 0, "Data index is invalid.");
        EXEC_WHEN_DEBUG(
            l.in("Frame").debug("Setting data of graph {} with index {}", graph_->name(), index));

        if (index < 0) {
            return graph_->setStaticData(index, value);
        }

        size_t idx = static_cast<size_t>(index) - 1;
        ASSERT(idx < graph_->argsCount(), "Invalid argument index");
        *reinterpret_cast<T *>(data_[idx]) = std::forward<T>(value);
    }

    std::string toString() const;

    ObjectHeader *header() const {
        // Frame 引用的内存块由自己的虚构函数释放，不通过GC收集
        return nullptr;
    }

    void traverse(const std::function<void(GCObject *)> &visit) const override {
        for (size_t i = 0; i < layout_->size(); ++i) {
            if (isGCTraced(layout_->typeCode(i))) {
                auto objPtr = *reinterpret_cast<GCObject *const *>(data_[i]);
                if (objPtr) {
                    visit(objPtr);
                }
            }
        }
    }

  private:
    void **data_;                       // 指针数组区
    const CompositeDataLayout *layout_; // 直接复用
    GraphIR::Graph *graph_;
    IAllocator *allocator_;
};
