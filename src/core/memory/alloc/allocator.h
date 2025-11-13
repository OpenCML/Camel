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

#include "header.h"

#include <functional>

class IAllocator {
  public:
    virtual ~IAllocator() = default;

    // 分配内存，可指定对齐
    virtual void *alloc(size_t size, size_t align) = 0;

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
