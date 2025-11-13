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
 * Updated: Nov. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "allocator.h"
#include "header.h"
#include "utils/assert.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

class FreeListAllocator : public IAllocator {
  public:
    FreeListAllocator(size_t capacity) : capacity_(capacity) {
        buffer_ = std::make_unique<uint8_t[]>(capacity_);
        start_  = buffer_.get();
        end_    = start_ + capacity_;

        freeList_       = reinterpret_cast<FreeBlock *>(start_);
        freeList_->size = capacity_;
        freeList_->next = nullptr;
    }

    void *alloc(size_t size, size_t align) override {
        ASSERT((align & (align - 1)) == 0, "Alignment must be a power of two");

        size_t total_align = adjustAlign(align);

        FreeBlock *prev = nullptr;
        FreeBlock *curr = freeList_;

        while (curr) {
            uint8_t *blockStart  = reinterpret_cast<uint8_t *>(curr);
            uint8_t *alignedData = alignPointer(blockStart + sizeof(FreeBlock), total_align);
            size_t padding       = alignedData - blockStart;
            size_t needed        = padding + sizeof(ObjectHeader) + size;

            if (curr->size >= needed) {
                uint8_t *allocStart = blockStart + padding;
                installHeader(allocStart, needed);

                size_t remaining = curr->size - needed;
                if (remaining >= sizeof(FreeBlock)) {
                    FreeBlock *newBlock = reinterpret_cast<FreeBlock *>(blockStart + needed);
                    newBlock->size      = remaining;
                    newBlock->next      = curr->next;
                    if (prev)
                        prev->next = newBlock;
                    else
                        freeList_ = newBlock;
                } else {
                    if (prev)
                        prev->next = curr->next;
                    else
                        freeList_ = curr->next;
                }
                return allocStart + sizeof(ObjectHeader);
            }

            prev = curr;
            curr = curr->next;
        }
        return nullptr;
    }

    void free(void *ptr) override {
        if (!ptr)
            return;

        uint8_t *blockData   = reinterpret_cast<uint8_t *>(ptr) - sizeof(ObjectHeader);
        ObjectHeader *header = reinterpret_cast<ObjectHeader *>(blockData);
        size_t total_size    = header->size();

        insertFreeBlock(blockData, total_size);
    }

    void freeBulk(const std::vector<ObjectHeader *> &objects) override {
        for (auto *hdr : objects) {
            uint8_t *blockData = reinterpret_cast<uint8_t *>(hdr);
            insertFreeBlock(blockData, hdr->size());
        }
    }

    void reset() override {
        freeList_       = reinterpret_cast<FreeBlock *>(start_);
        freeList_->size = capacity_;
        freeList_->next = nullptr;
    }

    size_t available() const override {
        size_t total    = 0;
        FreeBlock *curr = freeList_;
        while (curr) {
            total += curr->size;
            curr = curr->next;
        }
        return total;
    }

    bool contains(void *ptr) const override { return ptr >= start_ && ptr < end_; }

    void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const override {
        uint8_t *curr = start_;
        FreeBlock *fb = freeList_;

        while (curr < end_) {
            // 如果当前地址是空闲块起始
            if (fb && curr == reinterpret_cast<uint8_t *>(fb)) {
                curr += fb->size; // 跳过空闲块
                fb = fb->next;    // 移动到下一个空闲块
            } else {
                ObjectHeader *hdr = reinterpret_cast<ObjectHeader *>(curr);
                visitor(hdr);
                curr += hdr->size();
            }
        }
    }

  private:
    struct FreeBlock {
        size_t size;
        FreeBlock *next;
    };

    size_t capacity_;
    std::unique_ptr<uint8_t[]> buffer_;
    uint8_t *start_;
    uint8_t *end_;
    FreeBlock *freeList_;

    void insertFreeBlock(uint8_t *blockData, size_t total_size) {
        FreeBlock *block = reinterpret_cast<FreeBlock *>(blockData);
        block->size      = total_size;
        block->next      = nullptr;

        // 按地址有序插入
        FreeBlock *prev = nullptr;
        FreeBlock *curr = freeList_;
        while (curr && curr < block) {
            prev = curr;
            curr = curr->next;
        }

        block->next = curr;
        if (prev)
            prev->next = block;
        else
            freeList_ = block;

        // 合并前后
        if (prev &&
            reinterpret_cast<uint8_t *>(prev) + prev->size == reinterpret_cast<uint8_t *>(block)) {
            prev->size += block->size;
            prev->next = block->next;
            block      = prev;
        }

        if (curr &&
            reinterpret_cast<uint8_t *>(block) + block->size == reinterpret_cast<uint8_t *>(curr)) {
            block->size += curr->size;
            block->next = curr->next;
        }
    }
};
