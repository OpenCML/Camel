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
 * Updated: Nov. 23, 2025
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
        // capacity 是字节数，向上对齐
        size_t aligned_capacity = alignUp(capacity, alignof(slot_t));
        size_t num_units        = aligned_capacity / sizeof(slot_t);

        buffer_ = std::make_unique<slot_t[]>(num_units);
        start_  = reinterpret_cast<std::byte *>(buffer_.get());
        top_    = start_;
        end_    = start_ + aligned_capacity; // 实际可用字节数
    }

    void *alloc(size_t size, size_t align = alignof(slot_t)) override {
        ASSERT(align == alignof(slot_t), "Alignment other than 8 bytes is not supported");

        // total_size 向上对齐
        // 这保证了 top_ 始终对齐，无需每次都 alignPointer
        size_t total_size = alignUp(sizeof(ObjectHeader) + size, alignof(slot_t));

        std::byte *newTop = top_ + total_size;

        if (UNLIKELY(newTop > end_)) {
            return nullptr;
        }

        // 安装对象头，记录的是对齐后的 total_size
        installHeader(top_, total_size);

        std::byte *result = top_ + sizeof(ObjectHeader);
        top_              = newTop;

        return result;
    }

    void free(void * /*ptr*/) override {
        ASSERT(false, "Bump pointer allocator does not support single free");
    }

    void reset() override { top_ = start_; }

    size_t available() const override {
        return top_ <= end_ ? static_cast<size_t>(end_ - top_) : 0;
    }

    bool contains(void *ptr) const override { return ptr >= start_ && ptr < end_; }

    std::byte *start() const { return start_; }
    std::byte *top() const { return top_; }
    std::byte *end() const { return end_; }

    void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const override {
        std::byte *current = start_;
        while (current < top_) {
            ObjectHeader *header = reinterpret_cast<ObjectHeader *>(current);

            // 验证 header 的合法性
            size_t obj_size = header->size();
            ASSERT(obj_size >= sizeof(ObjectHeader), "Invalid object size");
            ASSERT(obj_size % alignof(slot_t) == 0, "Object size not aligned");
            ASSERT(current + obj_size <= top_, "Object extends beyond top");

            visitor(header);
            current += obj_size;
        }
        ASSERT(current == top_, "Iterator did not reach top exactly");
    }

    void freeBulk(const std::vector<ObjectHeader *> & /*objects*/) override {
        ASSERT(false, "Bump pointer allocator does not support bulk free");
    }

  private:
    size_t capacity_;
    std::unique_ptr<uint64_t[]> buffer_;
    std::byte *start_;
    std::byte *top_;
    std::byte *end_;
};
