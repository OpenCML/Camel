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
 * Updated: Nov. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "allocator.h"
#include "header.h"
#include "utils/assert.h"

#include <cstddef>
#include <limits> // for std::numeric_limits
#include <new>    // for ::operator new / ::operator delete
#include <unordered_set>

class LargeObjectAllocator : public IAllocator {
  public:
    ~LargeObjectAllocator() override {
        for (auto *hdr : allocated_) {
            ::operator delete(hdr, std::align_val_t(alignof(std::max_align_t)));
        }
        allocated_.clear();
    }

    void *alloc(size_t size, size_t align) override {
        size_t total_align = adjustAlign(align);
        size_t total_size = OBJECT_HEADER_SIZE + size;

        uint8_t *raw =
            reinterpret_cast<uint8_t *>(::operator new(total_size, std::align_val_t(total_align)));

        auto *hdr = reinterpret_cast<ObjectHeader *>(raw);
        hdr->setSize(total_size);

        allocated_.insert(hdr);

        return raw + OBJECT_HEADER_SIZE;
    }

    void free(void *ptr) override {
        if (!ptr)
            return;

        uint8_t *blockData = reinterpret_cast<uint8_t *>(ptr) - OBJECT_HEADER_SIZE;
        auto *hdr = reinterpret_cast<ObjectHeader *>(blockData);

        auto it = allocated_.find(hdr);
        if (it != allocated_.end()) {
            allocated_.erase(it);
        }

        ::operator delete(hdr, std::align_val_t(alignof(std::max_align_t)));
    }

    void reset() override {
        for (auto *hdr : allocated_) {
            ::operator delete(hdr, std::align_val_t(alignof(std::max_align_t)));
        }
        allocated_.clear();
    }

    size_t available() const override { return std::numeric_limits<size_t>::max(); }

    bool contains(void *ptr) const override {
        if (!ptr)
            return false;
        uint8_t *blockData = reinterpret_cast<uint8_t *>(ptr) - OBJECT_HEADER_SIZE;
        auto *hdr = reinterpret_cast<const ObjectHeader *>(blockData);
        return allocated_.find(const_cast<ObjectHeader *>(hdr)) != allocated_.end();
    }

    void freeBulk(const std::vector<ObjectHeader *> &objects) override {
        for (auto *hdr : objects) {
            auto it = allocated_.find(hdr);
            if (it != allocated_.end()) {
                allocated_.erase(it);
                ::operator delete(hdr, std::align_val_t(alignof(std::max_align_t)));
            }
        }
    }

    void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const override {
        for (auto *hdr : allocated_) {
            visitor(hdr);
        }
    }

  private:
    std::unordered_set<ObjectHeader *> allocated_;
};
