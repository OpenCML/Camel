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
 * Updated: Nov. 04, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "arena.h"
#include "utils/log.h"

class Frame;
using frame_rptr_t = Frame *;

// FrameTemplate 用来描述某个 Frame 的内存布局模板
// 它不分配内存，只负责计算参数指针区和数据区的偏移以及总大小
struct FrameTemplate {
    GraphIR::Graph *graph;       // 指向关联的计算图对象
    std::vector<size_t> offsets; // 每个参数在数据区中的偏移
    size_t dataOffset;           // 数据区相对于整个 Frame 的起始偏移（前面是指针数组）
    size_t totalSize;            // 整个 Frame 内存块的总大小（已按 cache line 对齐）

    FrameTemplate(GraphIR::Graph *g);

    size_t argsCount() const { return offsets.size(); }
};

namespace GraphIR {
class Graph;
using data_idx_t = int16_t;
} // namespace GraphIR

// Frame 是实际持有内存的对象，基于 FrameTemplate 分配并初始化布局
class Frame {
  public:
    Frame(const FrameTemplate &temp, IAllocator &allocator);

    ~Frame() {
        if (allocator_ && data_) {
            allocator_->free(data_);
        }
    }

    const GraphIR::Graph *graph() const { return graph_; }

    template <typename T> T *get(data_idx_t index) {
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

    template <typename T> void set(data_idx_t index, T &&value) {
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

  private:
    // 指针数组区 [ args ptr array ][ args data block ]
    void **data_;
    GraphIR::Graph *graph_;
    IAllocator *allocator_;
};
