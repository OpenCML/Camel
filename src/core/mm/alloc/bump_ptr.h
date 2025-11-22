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
 * Created: Nov. 07, 2025
 * Updated: Nov. 22, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "allocator.h"
#include "header.h"
#include "utils/assert.h"
#include "utils/brpred.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

class BumpPointerAllocator : public IAllocator {
  public:
    BumpPointerAllocator(size_t capacity) : capacity_(capacity) {
        buffer_ = std::make_unique<uint8_t[]>(capacity_);
        start_  = buffer_.get();
        top_    = start_;
        end_    = start_ + capacity_;
    }

    void *alloc(size_t size, size_t align = alignof(std::max_align_t)) override {
        ASSERT((align & (align - 1)) == 0, "Alignment must be a power of two");

        // 保证对象头 + 数据区整体对齐
        size_t total_align  = adjustAlign(align);
        uint8_t *alignedTop = alignPointer(top_, total_align);

        size_t total_size = sizeof(ObjectHeader) + size;
        uint8_t *newTop   = alignedTop + total_size;

        if (UNLIKELY(newTop > end_)) {
            return nullptr;
        }

        installHeader(alignedTop, total_size);

        top_ = newTop;
        return alignedTop + sizeof(ObjectHeader); // 返回数据区指针
    }

    void free(void * /*ptr*/) override {
        // Bump pointer 分配器不支持单个释放
        ASSERT(false, "Bump pointer allocator does not support single free");
    }

    void reset() override { top_ = start_; }

    size_t available() const override { return static_cast<size_t>(end_ - top_); }

    bool contains(void *ptr) const override { return ptr >= start_ && ptr < end_; }

    uint8_t *start() const { return start_; }
    uint8_t *top() const { return top_; }
    uint8_t *end() const { return end_; }

    void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const override {
        uint8_t *current = start_;
        while (current < top_) {
            ObjectHeader *header = reinterpret_cast<ObjectHeader *>(current);
            visitor(header);
            current += header->size();
        }
    }

    void freeBulk(const std::vector<ObjectHeader *> & /*objects*/) override {
        // Bump pointer 分配器不支持单个释放，故此方法为空实现
        ASSERT(false, "Bump pointer allocator does not support bulk free");
    }

  private:
    size_t capacity_;
    std::unique_ptr<uint8_t[]> buffer_;
    uint8_t *start_;
    uint8_t *top_;
    uint8_t *end_;
};
