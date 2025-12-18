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
 * Updated: Dec. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "header.h"
#include "utils/assert.h"

#include <functional>
#include <type_traits>
#include <utility> // for std::forward

constexpr size_t KB = 1024;
constexpr size_t MB = 1024 * KB;
constexpr size_t GB = 1024 * MB;

class IAllocator {
  public:
    virtual ~IAllocator() = default;

    // 分配内存，可指定对齐
    virtual void *alloc(size_t size, size_t align = alignof(slot_t)) = 0;

    // 释放单个对象（在 GC 新生代里可能是空实现）
    virtual void free(void *ptr) = 0;

    // 批量清空（适合半空间 / bump pointer 分配器）
    virtual void reset() { ASSERT(false, "reset() not implemented for this allocator"); };

    // 查询剩余可用空间
    virtual size_t available() const {
        ASSERT(false, "available() not implemented for this allocator");
        return 0;
    };

    // 判断指针是否属于该分配器管理的区域
    virtual bool contains(void *ptr) const {
        ASSERT(false, "contains() not implemented for this allocator");
        return false;
    };

    // 遍历已分配对象（只读，不允许在回调中直接free）
    virtual void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const {
        ASSERT(false, "iterateAllocated() not implemented for this allocator");
    };

    // 批量释放给定对象头列表
    virtual void freeBulk(const std::vector<ObjectHeader *> &objects) {
        for (auto *obj : objects) {
            free(reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(obj) + sizeof(ObjectHeader)));
        }
    };
};

template <typename T, typename... Args>
inline T *constructAt(IAllocator &allocator, Args &&...args) {
    // 分配内存，按 T 的对齐方式对齐
    void *ptr = allocator.alloc(sizeof(T), alignof(T));
    if (!ptr) {
        throw std::bad_alloc();
    }

    // 用 placement new 在分配的内存地址上构造对象
    return new (ptr) T(std::forward<Args>(args)...);
}

inline size_t alignUp(size_t n, size_t align) { return (n + align - 1) & ~(align - 1); }

template <typename T> inline std::string formatAddress(T *ptr, bool half = false) {
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);

    std::stringstream ss;
    ss << std::hex << std::uppercase << addr;
    std::string hexStr = ss.str();

    if (half) {
        if (hexStr.length() < 8) {
            hexStr = std::string(8 - hexStr.length(), '0') + hexStr;
        } else {
            hexStr = hexStr.substr(hexStr.length() - 8);
        }
    } else {
        if (hexStr.length() < 16) {
            hexStr = std::string(16 - hexStr.length(), '0') + hexStr;
        }
    }

    std::string formatted;
    for (size_t i = 0; i < hexStr.length(); ++i) {
        formatted += hexStr[i];
        if ((i + 1) % 4 == 0 && i + 1 != hexStr.length())
            formatted += '\'';
    }

    return "0x" + formatted;
}
