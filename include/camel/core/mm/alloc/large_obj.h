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
 * Updated: Feb. 23, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "allocator.h"
#include "camel/utils/assert.h"
#include "camel/utils/brpred.h"
#include "header.h"

#ifndef NDEBUG
#include "camel/core/mm/debug_hook.h"
#endif

#include <cstddef>
#include <limits> // for std::numeric_limits
#include <new>    // for ::operator new / ::operator delete
#include <unordered_set>

class LargeObjectAllocator : public IAllocator {
  public:
    explicit LargeObjectAllocator(const char *debugRegion = nullptr) : debugRegion_(debugRegion) {}

    ~LargeObjectAllocator() override {
        for (auto *hdr : allocated_) {
            ::operator delete(hdr, std::align_val_t(alignof(slot_t)));
        }
        allocated_.clear();
    }

    void *alloc(size_t size, size_t align = alignof(slot_t)) override {
        ASSERT(align == alignof(slot_t), "Alignment other than 8 bytes is not supported");

        // total_size 向上对齐到 slot_t
        size_t total_size = alignUp(sizeof(ObjectHeader) + size, alignof(slot_t));

        std::byte *raw = reinterpret_cast<std::byte *>(
            ::operator new(total_size, std::align_val_t(alignof(slot_t))));

        // 安装对象头，记录对齐后的 total_size
        installHeader(raw, total_size);

        std::byte *result = raw + sizeof(ObjectHeader);
        allocated_.insert(reinterpret_cast<ObjectHeader *>(raw));
#ifndef NDEBUG
        if (debugRegion_) {
            mm::invokePostAllocHook(mm::AllocEvent{result, total_size, debugRegion_});
        }
#endif
        return result;
    }

    void free(void *ptr) override {
        if (UNLIKELY(!ptr)) {
            return;
        }

        ObjectHeader *hdr = headerOf(ptr);

        auto it = allocated_.find(hdr);
        if (LIKELY(it != allocated_.end())) {
            allocated_.erase(it);
            ::operator delete(hdr, std::align_val_t(alignof(slot_t)));
        }
    }

    void reset() override {
        for (auto *hdr : allocated_) {
            ::operator delete(hdr, std::align_val_t(alignof(slot_t)));
        }
        allocated_.clear();
    }

    size_t available() const override { return std::numeric_limits<size_t>::max(); }

    bool contains(void *ptr) const override {
        if (UNLIKELY(!ptr)) {
            return false;
        }

        ObjectHeader *hdr = headerOf(ptr);
        return allocated_.find(hdr) != allocated_.end();
    }

    void freeBulk(const std::vector<ObjectHeader *> &objects) override {
        for (auto *hdr : objects) {
            auto it = allocated_.find(hdr);
            if (it != allocated_.end()) {
                allocated_.erase(it);
                ::operator delete(hdr, std::align_val_t(alignof(slot_t)));
            }
        }
    }

    void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const override {
        for (auto *hdr : allocated_) {
            // 验证 header 的合法性
            ASSERT(hdr->size() >= sizeof(ObjectHeader), "Invalid object size");
            ASSERT(hdr->size() % alignof(slot_t) == 0, "Object size not aligned");

            visitor(hdr);
        }
    }

  private:
    const char *debugRegion_{nullptr}; // Debug 模式下用于 hook
    std::unordered_set<ObjectHeader *> allocated_;
};
