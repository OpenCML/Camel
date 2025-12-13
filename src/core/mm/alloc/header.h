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
 * Updated: Dec. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "utils/assert.h"

#include <algorithm>
#include <cstdint>
#include <sstream>

using slot_t                                    = uint64_t;
static constexpr slot_t kDebugUninitializedSlot = 0xDEADBEEFDEADBEEFULL;

struct ObjectHeader {
    union {
        size_t raw_;
        struct {
            size_t total_ : 48;   // 正常模式存总大小（header+payload），GC模式存转发地址
            size_t age_ : 4;      // 年龄 (0-15)
            size_t marked_ : 1;   // GC标志
            size_t state_ : 1;    // 0=Normal, 1=Forwarded
            size_t region_ : 4;   // 所在区域
            size_t reserved_ : 6; // 预留位
        };
    };

    ObjectHeader() : raw_(0) {}
    ObjectHeader(size_t size, size_t age = 0, size_t region = 0) : raw_(0) {
        total_  = size & 0x0000FFFFFFFFFFFFULL;
        age_    = age;
        region_ = region;
    }

    ObjectHeader &operator=(const ObjectHeader &other) {
        raw_ = other.raw_;
        return *this;
    }

    size_t size() const {
        ASSERT(state_ == 0, "Cannot get size from forwarded object");
        return total_;
    }
    size_t age() const { return age_; }
    bool marked() const { return marked_ == 1; }
    bool forwarded() const { return state_ == 1; }
    size_t region() const { return region_; }

    void setSize(size_t size) {
        ASSERT(size <= 0x0000FFFFFFFFFFFFULL, "Size exceeds 48-bit limit");
        ASSERT(size >= sizeof(ObjectHeader), "Size too small");
        total_ = size & 0x0000FFFFFFFFFFFFULL;
        state_ = 0;
    }
    size_t objSize() const {
        ASSERT(state_ == 0, "Cannot get object size from forwarded object");
        return static_cast<size_t>(total_ - sizeof(ObjectHeader));
    }

    void incAge() {
        if (age_ < 15)
            age_++;
    }
    void setAge(size_t age) {
        ASSERT(age <= 15, "Age must be between 0 and 15");
        age_ = age;
    }
    void resetAge() { age_ = 0; }

    void mark() { marked_ = 1; }
    void unmark() { marked_ = 0; }

    void forward(void *addr) {
        ASSERT(addr != nullptr, "Cannot forward to null");

        size_t addrValue = reinterpret_cast<size_t>(addr);

        // 确保是规范的 48 位地址（x86-64）
        ASSERT(
            (addrValue & 0xFFFF000000000000ULL) == 0 ||
                (addrValue & 0xFFFF000000000000ULL) == 0xFFFF000000000000ULL,
            "Address must be canonical 48-bit");

        total_ = addrValue & 0x0000FFFFFFFFFFFFULL;
        state_ = 1;
    }

    void setRegion(size_t rid) {
        ASSERT(rid <= 15, "Region ID out of range");
        region_ = rid & 0x0F;
    }

    void *forwardedAddr() const {
        ASSERT(state_ == 1, "Object is not forwarded");
        // 符号扩展恢复完整地址
        size_t addrValue = total_;
        if (addrValue & 0x0000800000000000ULL) {
            addrValue |= 0xFFFF000000000000ULL;
        }
        return reinterpret_cast<void *>(addrValue);
    }

    bool isValid() const {
        if (state_ > 1)
            return false;
        if (state_ == 1 && total_ == 0)
            return false;
        if (state_ == 0 && total_ < sizeof(ObjectHeader))
            return false;
        if (region_ > 15)
            return false;
        return true;
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "ObjectHeader{"
            << "state=" << (state_ ? "Forwarded" : "Normal") << ", payload=0x" << std::hex << total_
            << std::dec << ", age=" << static_cast<int>(age_)
            << ", marked=" << static_cast<int>(marked_) << ", region=" << static_cast<int>(region_)
            << "}";
        return oss.str();
    }
};

// 静态断言确保大小
static_assert(sizeof(ObjectHeader) == 8, "ObjectHeader must be 8 bytes");

inline std::byte *alignPointer(std::byte *ptr, size_t align) {
    uintptr_t p       = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned = (p + align - 1) & ~(align - 1);
    return reinterpret_cast<std::byte *>(aligned);
}

inline void installHeader(std::byte *addr, size_t total_alloc_size) {
    auto *header = reinterpret_cast<ObjectHeader *>(addr);
    new (header) ObjectHeader(total_alloc_size);
}

template <typename T = void> inline T *payloadOf(ObjectHeader *header) {
    return reinterpret_cast<T *>(header + 1);
}

template <typename T = void> inline ObjectHeader *headerOf(T *payload) {
    return reinterpret_cast<ObjectHeader *>(payload) - 1;
}
