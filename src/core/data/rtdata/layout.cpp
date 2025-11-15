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

#include "layout.h"

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

StructDataLayout *StructDataLayout::create(
    IAllocator *allocator, TypeCode self,
    const std::vector<std::pair<std::string, DataLayout>> &fields) {

    uint16_t elemCnt = fields.size();

    // 计算需要的内存大小
    size_t baseSize    = sizeof(StructDataLayout);
    size_t offsetsSize = elemCnt * sizeof(uint16_t);
    size_t typesSize   = elemCnt * sizeof(TypeCode);
    size_t namesSize   = elemCnt * sizeof(const char *);

    // 分配内存
    size_t totalSize = baseSize + offsetsSize + typesSize + namesSize;
    void *mem        = allocator->alloc(totalSize, alignof(StructDataLayout));

    // 构造对象
    auto *layout = new (mem) StructDataLayout(self, elemCnt);

    // 设置数组指针（紧跟在对象后面）
    uint8_t *ptr = reinterpret_cast<uint8_t *>(layout + 1);

    layout->offsets_ = reinterpret_cast<uint16_t *>(ptr);
    ptr += offsetsSize;

    layout->types_ = reinterpret_cast<TypeCode *>(ptr);
    ptr += typesSize;

    layout->fieldNames_ = reinterpret_cast<const char **>(ptr);

    // 提取 DataLayout 向量
    std::vector<DataLayout> layouts;
    layouts.reserve(elemCnt);
    for (const auto &[name, layout] : fields) {
        layouts.push_back(layout);
    }

    // 调用父类的 build 来填充 offsets 和 types
    layout->build(layouts);

    // 填充字段名
    layout->buildWithNames(fields);

    return layout;
}

void StructDataLayout::buildWithNames(
    const std::vector<std::pair<std::string, DataLayout>> &fields) {

    // 字段名可以：
    // 1. 指向外部字符串池
    // 2. 分配新的字符串存储
    // 3. 使用内联存储（小字符串优化）

    for (size_t i = 0; i < fields.size(); ++i) {
        const auto &[name, layout] = fields[i];

        // 存储字段名（这里假设使用字符串池或持久化存储）
        fieldNames_[i] = name.c_str(); // 需要确保生命周期

        // 建立索引映射
        fieldMap_[fieldNames_[i]] = static_cast<uint16_t>(i);
    }
}
