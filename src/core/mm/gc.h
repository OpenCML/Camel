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
 * Updated: Dec. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/header.h"

#include <functional>

class GCObject;

using GCRef             = GCObject *;
constexpr GCRef NullRef = nullptr;

static_assert(sizeof(GCRef) == sizeof(slot_t), "GCRef size mismatch");

class GCObject {
  public:
    virtual ~GCObject()                                                                   = default;
    virtual bool equals(const GCRef other, bool deep = false) const                       = 0;
    virtual GCRef clone(IAllocator &allocator = mm::autoSpace(), bool deep = false) const = 0;
    virtual void onMoved()                                                                = 0;
    virtual void updateRefs(const std::function<GCRef(GCRef)> &relocate)                  = 0;

    // template <typename T> void setField(T *&field, T *newValue, GenerationalAllocatorWithGC *gc)
    // {
    //     ObjectHeader *thisHeader = headerOf(this);

    //     if (newValue) {
    //         ObjectHeader *newHeader = headerOf(newValue);

    //         // 写屏障：如果老年代对象引用年轻代对象
    //         if (gc->inElderGenSpace(thisHeader) && gc->inYoungGenSpace(newHeader)) {
    //             gc->recordOldToYoungRef(this, newValue);
    //         }
    //     }

    //     field = newValue;
    // }
};

template <typename T> inline bool isOfSameCls(const T *a, const T *b) noexcept {
    if (!a || !b)
        return false;
    // 借助 vtable 指针判断是否为同一对象
    auto vptrA = *reinterpret_cast<void *const *>(a);
    auto vptrB = *reinterpret_cast<void *const *>(b);
    return vptrA == vptrB;
}
