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

#include "base.h"

#include "array.h"
#include "string.h"

LayoutInfo getLayoutInfoOfType(TypeCode code) {
    switch (code) {
    case TypeCode::Int:
        return {sizeof(Int), alignof(Int)};
    case TypeCode::Long:
        return {sizeof(Long), alignof(Long)};
    case TypeCode::Float:
        return {sizeof(Float), alignof(Float)};
    case TypeCode::Double:
        return {sizeof(Double), alignof(Double)};
    case TypeCode::Bool:
        return {sizeof(Bool), alignof(Bool)};
    case TypeCode::Byte:
        return {sizeof(Byte), alignof(Byte)};
    case TypeCode::String:
        return {sizeof(GCString), alignof(GCString)};
    default:
        throw std::invalid_argument("Invalid TypeCode");
    }
}

CompositeDataLayout *CompositeDataLayout::create(
    IAllocator *allocator, TypeCode self, const std::vector<DataLayout> &elements) {
    uint16_t elemCnt = static_cast<uint16_t>(elements.size());

    // 计算内存布局
    size_t baseSize    = sizeof(CompositeDataLayout);
    size_t offsetsSize = sizeof(uint16_t) * elemCnt;
    size_t typesSize   = sizeof(TypeCode) * elemCnt;

    size_t offsetsStart = alignUp(baseSize, alignof(uint16_t));
    size_t typesStart   = alignUp(offsetsStart + offsetsSize, alignof(TypeCode));
    size_t totalSize    = typesStart + typesSize;

    // 一次性分配整块内存
    void *mem = allocator->alloc(totalSize, alignof(CompositeDataLayout));
    if (!mem)
        return nullptr;

    // 在已分配内存上构造对象
    auto *obj = new (mem) CompositeDataLayout(self, elemCnt);

    // 设置数组指针
    obj->offsets_ = reinterpret_cast<uint16_t *>(reinterpret_cast<char *>(obj) + offsetsStart);
    obj->types_   = reinterpret_cast<TypeCode *>(reinterpret_cast<char *>(obj) + typesStart);

    // 填充数据
    obj->build(elements);

    return obj;
}

void CompositeDataLayout::build(const std::vector<DataLayout> &elements) {
    size_t currentOffset = 0;
    size_t maxAlign      = 1;

    for (uint16_t i = 0; i < elemCnt_; ++i) {
        const auto &dl = elements[i];
        types_[i]      = dl.code();

        currentOffset = alignUp(currentOffset, dl.align());
        offsets_[i]   = static_cast<uint16_t>(currentOffset);
        currentOffset += dl.size();
        maxAlign = std::max(maxAlign, static_cast<size_t>(dl.align()));
    }

    size_  = static_cast<uint16_t>(alignUp(currentOffset, maxAlign));
    align_ = static_cast<uint16_t>(maxAlign);
}
