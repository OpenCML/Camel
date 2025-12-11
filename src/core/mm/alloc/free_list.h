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
 * Updated: Dec. 11, 2025
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
#include <unordered_set>

class FreeListAllocator : public IAllocator {
  public:
    explicit FreeListAllocator(size_t capacity) : capacity_(capacity) {
        size_t aligned_capacity = alignUp(capacity, alignof(slot_t));
        size_t num_units        = aligned_capacity / sizeof(slot_t);

        buffer_ = std::make_unique<slot_t[]>(num_units);
        start_  = reinterpret_cast<std::byte *>(buffer_.get());
        end_    = start_ + aligned_capacity;

        freeList_       = reinterpret_cast<FreeBlock *>(start_);
        freeList_->size = aligned_capacity;
        freeList_->next = nullptr;
    }

    void *alloc(size_t size, size_t align = alignof(slot_t)) override {
        ASSERT(align == alignof(slot_t), "Alignment other than 8 bytes is not supported");

        size_t total_size = alignUp(sizeof(ObjectHeader) + size, alignof(slot_t));

        FreeBlock **prevPtr = &freeList_;
        FreeBlock *curr     = freeList_;

        while (curr) {
            ASSERT(curr->size >= sizeof(FreeBlock), "Corrupted free block");
            ASSERT(curr->size % alignof(slot_t) == 0, "Unaligned free block");

            if (LIKELY(curr->size >= total_size)) {
                std::byte *blockStart = reinterpret_cast<std::byte *>(curr);
                size_t remaining      = curr->size - total_size;

                if (LIKELY(remaining >= sizeof(FreeBlock))) {
                    // 分割块
                    installHeader(blockStart, total_size);

                    FreeBlock *newBlock = reinterpret_cast<FreeBlock *>(blockStart + total_size);
                    newBlock->size      = remaining;
                    newBlock->next      = curr->next;

                    *prevPtr = newBlock;
                } else {
                    // 整块分配
                    installHeader(blockStart, curr->size);
                    *prevPtr = curr->next;
                }

                return blockStart + sizeof(ObjectHeader);
            }

            prevPtr = &(curr->next);
            curr    = curr->next;
        }

        return nullptr;
    }

    void free(void *ptr) override {
        if (UNLIKELY(!ptr))
            return;

        ObjectHeader *header = headerOf(ptr);
        size_t total_size    = header->size();

        ASSERT(total_size % alignof(slot_t) == 0, "Object size not aligned");
        ASSERT(total_size >= sizeof(ObjectHeader), "Invalid object size");

        std::byte *blockData = reinterpret_cast<std::byte *>(header);

        // 检测重复释放：检查是否与任何空闲块重叠
        EXEC_WHEN_DEBUG([&]() {
            FreeBlock *fb = freeList_;
            while (fb) {
                std::byte *fb_start  = reinterpret_cast<std::byte *>(fb);
                std::byte *fb_end    = fb_start + fb->size;
                std::byte *block_end = blockData + total_size;

                // 检查重叠（不包括相邻情况）
                ASSERT(!(blockData < fb_end && block_end > fb_start), "Double free detected");

                fb = fb->next;
            }
        }());

        insertAndCoalesce(blockData, total_size);
    }

    void freeBulk(const std::vector<ObjectHeader *> &objects) override {
        if (UNLIKELY(objects.empty()))
            return;

        std::vector<std::pair<std::byte *, size_t>> blocks;
        blocks.reserve(objects.size());

        for (auto *hdr : objects) {
            size_t obj_size = hdr->size();
            ASSERT(obj_size % alignof(slot_t) == 0, "Object size not aligned");
            ASSERT(obj_size >= sizeof(ObjectHeader), "Invalid object size");
            blocks.emplace_back(reinterpret_cast<std::byte *>(hdr), obj_size);
        }

        // 按地址排序
        std::sort(blocks.begin(), blocks.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });

        // 检查重叠和重复
        for (size_t i = 1; i < blocks.size(); ++i) {
            ASSERT(blocks[i - 1].first != blocks[i].first, "Duplicate blocks in freeBulk");
            ASSERT(
                blocks[i - 1].first + blocks[i - 1].second <= blocks[i].first,
                "Overlapping blocks in freeBulk");
        }

        // 检测重复释放
        EXEC_WHEN_DEBUG([&]() {
            for (const auto &[addr, size] : blocks) {
                FreeBlock *fb = freeList_;
                while (fb) {
                    std::byte *fb_start  = reinterpret_cast<std::byte *>(fb);
                    std::byte *fb_end    = fb_start + fb->size;
                    std::byte *block_end = addr + size;

                    // 检查重叠
                    ASSERT(!(addr < fb_end && block_end > fb_start), "Double free in bulk");

                    fb = fb->next;
                }
            }
        }());

        // 逐个插入并合并
        for (const auto &[addr, size] : blocks) {
            insertAndCoalesce(addr, size);
        }
    }

    void reset() override {
        size_t total_capacity = end_ - start_;
        freeList_             = reinterpret_cast<FreeBlock *>(start_);
        freeList_->size       = total_capacity;
        freeList_->next       = nullptr;
    }

    size_t available() const override {
        size_t total    = 0;
        FreeBlock *curr = freeList_;
        std::unordered_set<const void *> visited;

        while (curr) {
            if (UNLIKELY(visited.find(curr) != visited.end())) {
                throw std::runtime_error("Cyclic free list detected");
            }
            visited.insert(curr);

            ASSERT(curr->size >= sizeof(FreeBlock), "Invalid free block size");
            total += curr->size;
            curr = curr->next;
        }
        return total;
    }

    bool contains(void *ptr) const override { return ptr >= start_ && ptr < end_; }

    void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const override {
        std::byte *current = start_;
        FreeBlock *fb      = freeList_;

        while (LIKELY(current < end_)) {
            // 当前位置是否是空闲块？
            if (UNLIKELY(fb && reinterpret_cast<std::byte *>(fb) == current)) {
                // 跳过空闲块
                ASSERT(fb->size >= sizeof(FreeBlock), "Free block too small");
                ASSERT(fb->size % alignof(slot_t) == 0, "Free block not aligned");
                ASSERT(current + fb->size <= end_, "Free block out of bounds");

                current += fb->size;
                fb = fb->next;
            } else {
                // 必须是已分配对象
                ASSERT(reinterpret_cast<std::byte *>(fb) > current, "Free list ordering error");

                ObjectHeader *header = reinterpret_cast<ObjectHeader *>(current);
                size_t obj_size      = header->size();

                ASSERT(obj_size >= sizeof(ObjectHeader), "Invalid object size");
                ASSERT(obj_size % alignof(slot_t) == 0, "Object not aligned");
                ASSERT(current + obj_size <= end_, "Object out of bounds");

                // 确保不与下一个空闲块重叠
                ASSERT(
                    current + obj_size <= reinterpret_cast<std::byte *>(fb),
                    "Object overlaps with free block");

                visitor(header);
                current += obj_size;
            }
        }

        ASSERT(current == end_, "Did not reach end");
        ASSERT(fb == nullptr, "Free list not fully consumed");
    }

    bool validate() const {
        try {
            // 步骤1：验证空闲链表自身
            std::unordered_set<const void *> freeBlockSet;
            FreeBlock *curr    = freeList_;
            FreeBlock *prev_fb = nullptr;
            size_t free_total  = 0;

            while (curr) {
                // 检测环路
                if (UNLIKELY(freeBlockSet.find(curr) != freeBlockSet.end())) {
                    return false;
                }
                freeBlockSet.insert(curr);

                std::byte *curr_start = reinterpret_cast<std::byte *>(curr);
                std::byte *curr_end   = curr_start + curr->size;

                // 边界检查
                if (UNLIKELY(curr_start < start_ || curr_end > end_)) {
                    return false;
                }

                // 大小和对齐检查
                if (UNLIKELY(curr->size < sizeof(FreeBlock) || curr->size % alignof(slot_t) != 0)) {
                    return false;
                }

                // 排序和无重叠检查
                if (UNLIKELY(prev_fb)) {
                    std::byte *prev_start = reinterpret_cast<std::byte *>(prev_fb);
                    std::byte *prev_end   = prev_start + prev_fb->size;

                    if (UNLIKELY(prev_start >= curr_start)) {
                        return false; // 未排序
                    }
                    if (UNLIKELY(prev_end > curr_start)) {
                        return false; // 重叠
                    }
                    // 检查相邻块是否应该合并
                    if (UNLIKELY(prev_end == curr_start)) {
                        return false; // 相邻块应该被合并
                    }
                }

                free_total += curr->size;
                prev_fb = curr;
                curr    = curr->next;
            }

            // 步骤2：扫描整个内存区域
            std::byte *scan        = start_;
            size_t allocated_total = 0;
            FreeBlock *expected_fb = freeList_;

            while (UNLIKELY(scan < end_)) {
                // 检查当前位置是否是空闲块
                if (UNLIKELY(expected_fb && reinterpret_cast<std::byte *>(expected_fb) == scan)) {
                    // 是空闲块
                    scan += expected_fb->size;
                    expected_fb = expected_fb->next;
                } else {
                    // 应该是已分配对象
                    if (UNLIKELY(
                            expected_fb && reinterpret_cast<std::byte *>(expected_fb) < scan)) {
                        return false; // 空闲链表顺序错误
                    }

                    ObjectHeader *header = reinterpret_cast<ObjectHeader *>(scan);
                    size_t obj_size      = header->size();

                    // 基本验证
                    if (UNLIKELY(
                            obj_size < sizeof(ObjectHeader) || obj_size % alignof(slot_t) != 0 ||
                            scan + obj_size > end_)) {
                        return false;
                    }

                    // 检查不与下一个空闲块重叠
                    if (UNLIKELY(expected_fb)) {
                        std::byte *next_fb_start = reinterpret_cast<std::byte *>(expected_fb);
                        if (UNLIKELY(scan + obj_size > next_fb_start)) {
                            return false;
                        }
                    }

                    allocated_total += obj_size;
                    scan += obj_size;
                }
            }

            // 步骤3：最终检查
            if (UNLIKELY(scan != end_)) {
                return false; // 未完全扫描
            }

            if (UNLIKELY(expected_fb != nullptr)) {
                return false; // 空闲链表有剩余
            }

            // 检查总和
            size_t total_capacity = end_ - start_;
            if (UNLIKELY(free_total + allocated_total != total_capacity)) {
                return false;
            }

            return true;

        } catch (...) {
            return false;
        }
    }

    std::byte *start() const { return start_; }
    std::byte *end() const { return end_; }

  private:
    struct FreeBlock {
        size_t size;
        FreeBlock *next;
    };

    size_t capacity_;
    std::unique_ptr<slot_t[]> buffer_;
    std::byte *start_;
    std::byte *end_;
    FreeBlock *freeList_;

    // 辅助函数：从用户指针获取对象头
    ObjectHeader *headerOf(void *ptr) const {
        return reinterpret_cast<ObjectHeader *>(
            static_cast<std::byte *>(ptr) - sizeof(ObjectHeader));
    }

    // 核心函数：插入并合并空闲块
    void insertAndCoalesce(std::byte *blockData, size_t total_size) {
        ASSERT(total_size >= sizeof(FreeBlock), "Block too small");
        ASSERT(blockData >= start_ && blockData + total_size <= end_, "Block out of bounds");

        FreeBlock **prevPtr = &freeList_;
        FreeBlock *prev     = nullptr;
        FreeBlock *curr     = freeList_;

        // 找到插入位置（保持按地址排序）
        while (curr && reinterpret_cast<std::byte *>(curr) < blockData) {
            prev    = curr;
            prevPtr = &(curr->next);
            curr    = curr->next;
        }

        std::byte *blockEnd = blockData + total_size;

        // 检查与后块合并
        if (LIKELY(curr) && UNLIKELY(blockEnd == reinterpret_cast<std::byte *>(curr))) {
            total_size += curr->size;
            curr = curr->next;
        }

        // 检查与前块合并
        if (LIKELY(prev)) {
            std::byte *prevEnd = reinterpret_cast<std::byte *>(prev) + prev->size;
            if (UNLIKELY(prevEnd == blockData)) {
                prev->size += total_size;
                prev->next = curr;
                return;
            }
        }

        // 否则，创建新的 free block
        FreeBlock *newBlock = reinterpret_cast<FreeBlock *>(blockData);
        newBlock->size      = total_size;
        newBlock->next      = curr;
        *prevPtr            = newBlock;
    }
};
