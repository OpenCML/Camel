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
 * Updated: Nov. 15, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "comp.h"

class GCStruct : public GCCompositeObject {
  public:
    explicit GCStruct(const StructDataLayout *layout, IAllocator &allocator = GlobalGC::instance())
        : GCCompositeObject(layout, allocator) {}

    // 安全的向下转型
    const StructDataLayout *structLayout() const {
        return static_cast<const StructDataLayout *>(layout_);
    }

    // 按名字访问 - 返回指针（统一接口，适用于所有类型）
    template <typename T> T *get(std::string_view name) {
        auto index = structLayout()->findField(name);
        if (!index)
            return nullptr;
        return &at<T>(*index);
    }

    template <typename T> const T *get(std::string_view name) const {
        auto index = structLayout()->findField(name);
        if (!index)
            return nullptr;
        return &at<T>(*index);
    }

    // 按名字访问 - 返回引用（更符合你的 at() 风格，但要确保字段存在）
    template <typename T> T &getRef(std::string_view name) {
        auto index = structLayout()->findField(name);
        ASSERT(index.has_value(), "Field not found");
        return at<T>(*index);
    }

    template <typename T> const T &getRef(std::string_view name) const {
        auto index = structLayout()->findField(name);
        ASSERT(index.has_value(), "Field not found");
        return at<T>(*index);
    }

    // 设置字段值（对于 GC 对象需要写屏障的话，可以在这里添加）
    template <typename T> void set(std::string_view name, const T &value) {
        auto index = structLayout()->findField(name);
        ASSERT(index.has_value(), "Field not found");
        at<T>(*index) = value;
    }

    // 获取字段名
    std::string_view fieldName(size_t index) const { return structLayout()->fieldName(index); }

    // 检查字段存在
    bool hasField(std::string_view name) const {
        return structLayout()->findField(name).has_value();
    }

    // 获取字段索引（用于缓存）
    std::optional<size_t> fieldIndex(std::string_view name) const {
        return structLayout()->findField(name);
    }
};
