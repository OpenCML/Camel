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

#include <algorithm>
#include <cstdint>

struct ObjectHeader {
    union {
        uint64_t raw;
        struct {
            uint64_t payload : 48;  // 正常模式存总大小(header+padding+block)，GC模式存转发地址
            uint64_t age : 4;       // 年龄
            uint64_t mark : 1;      // GC标志
            uint64_t state : 1;     // 0=Normal, 1=Forwarded
            uint64_t reserved : 10; // 其他预留位
        };
    };

    ObjectHeader() : raw(0) {}

    void setSize(uint64_t size) {
        payload = size;
        state = 0;
    }

    void forward(void *addr) {
        payload = reinterpret_cast<uint64_t>(addr) & 0x0000FFFFFFFFFFFFULL;
        state = 1;
    }

    bool forwarded() const { return state == 1; }

    uint64_t size() const { return payload; }

    void *addr() const { return (state == 1) ? reinterpret_cast<void *>(payload) : nullptr; }
};

static_assert(sizeof(ObjectHeader) == sizeof(uint64_t), "ObjectHeader size must be 8 bytes");

constexpr size_t OBJECT_HEADER_SIZE = sizeof(ObjectHeader);
constexpr size_t OBJECT_HEADER_ALIGN = alignof(ObjectHeader);

inline size_t adjustAlign(size_t align) { return std::max(align, OBJECT_HEADER_ALIGN); }

inline uint8_t *alignPointer(uint8_t *ptr, size_t align) {
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned = (p + align - 1) & ~(align - 1);
    return reinterpret_cast<uint8_t *>(aligned);
}

inline void installHeader(uint8_t *addr, size_t total_alloc_size) {
    auto *header = reinterpret_cast<ObjectHeader *>(addr);
    header->setSize(total_alloc_size);
    header->age = 0;
    header->mark = 0;
}
