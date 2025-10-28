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
 * Created: Jul. 21, 2025
 * Updated: Oct. 28, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/data/data.h"
#include "utils/assert.h"
#include "utils/log.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

class Arena {
  public:
    explicit Arena(size_t capacity) : capacity(capacity), buffer(new uint8_t[capacity]) {}

    ~Arena() { delete[] buffer; }

    void *base() { return buffer; }
    size_t size() const { return capacity; }

  private:
    size_t capacity;
    uint8_t *buffer;
};

class IAllocator {
  public:
    virtual ~IAllocator() = default;
    virtual void *alloc(size_t size) = 0;
    virtual void free(void *ptr) = 0;
};

class LinearAllocator : public IAllocator {
  public:
    explicit LinearAllocator(Arena &arena) : arena(arena), offset(0) {}

    void *alloc(size_t size) override {
        if (offset + size > arena.size()) {
            throw std::bad_alloc{};
        }
        void *ptr = static_cast<uint8_t *>(arena.base()) + offset;
        offset += size;
        return ptr;
    }

    void free(void *ptr) override {
        size_t pos = static_cast<uint8_t *>(ptr) - static_cast<uint8_t *>(arena.base());
        if (pos > offset)
            throw std::runtime_error("Invalid free in LinearAllocator");
        offset = pos;
    }

    size_t marker() const { return offset; }
    void freeToMarker(size_t m) { offset = m; }

  private:
    Arena &arena;
    size_t offset;
};

class PoolAllocator : public IAllocator {
  public:
    PoolAllocator(Arena &arena, size_t objectSize, size_t objectCount)
        : arena(arena), objectSize(objectSize), objectCount(objectCount) {
        if (objectSize < sizeof(void *)) {
            objectSize = sizeof(void *); // 确保能存放 free list 指针
        }
        initFreeList();
    }

    void *alloc(size_t size) override {
        if (size > objectSize) {
            throw std::bad_alloc{};
        }
        if (!freeList) {
            throw std::bad_alloc{};
        }
        void *ptr = freeList;
        freeList = *reinterpret_cast<void **>(freeList);
        return ptr;
    }

    void free(void *ptr) override {
        *reinterpret_cast<void **>(ptr) = freeList;
        freeList = ptr;
    }

  private:
    void initFreeList() {
        freeList = arena.base();
        uint8_t *current = static_cast<uint8_t *>(freeList);
        for (size_t i = 0; i < objectCount - 1; ++i) {
            uint8_t *next = current + objectSize;
            *reinterpret_cast<void **>(current) = next;
            current = next;
        }
        *reinterpret_cast<void **>(current) = nullptr; // 最后一个
    }

    Arena &arena;
    size_t objectSize;
    size_t objectCount;
    void *freeList = nullptr;
};
