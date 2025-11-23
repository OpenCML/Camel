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
 * Updated: Nov. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class CompositeDataLayout {
  public:
    CompositeDataLayout(TypeCode self, uint16_t elemCnt)
        : elemCnt_(elemCnt), offsets_(nullptr), types_(nullptr) {}
    static CompositeDataLayout *
    create(IAllocator *allocator, TypeCode self, const std::vector<TypeCode> &elements);

    uint16_t elemCnt() const { return elemCnt_; }
    const uint16_t *offsets() const { return offsets_; }
    const TypeCode *types() const { return types_; }
    const uint16_t offset(size_t index) const {
        ASSERT(index < elemCnt_, "Index out of range");
        return offsets_[index];
    }
    const TypeCode typeCode(size_t index) const {
        ASSERT(index < elemCnt_, "Index out of range");
        return types_[index];
    }

  protected:
    void build(const std::vector<TypeCode> &elements);

    uint16_t elemCnt_;
    uint16_t *offsets_;
    TypeCode *types_;
};

class StructDataLayout : public CompositeDataLayout {
  public:
    static StructDataLayout *create(
        IAllocator *allocator, TypeCode self,
        const std::vector<std::pair<std::string, TypeCode>> &fields);

    // 按名字查找字段索引
    std::optional<size_t> findField(std::string_view name) const {
        auto it = fieldMap_.find(name);
        if (it != fieldMap_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // 获取字段名
    std::string_view fieldName(size_t index) const {
        ASSERT(index < elemCnt(), "Index out of range");
        return fieldNames_[index];
    }

  private:
    StructDataLayout(TypeCode self, uint16_t elemCnt) : CompositeDataLayout(self, elemCnt) {}

    void buildWithNames(const std::vector<std::pair<std::string, TypeCode>> &fields);

    // 字段名数组（紧跟在 offsets/types 之后分配）
    const char **fieldNames_;

    // 名字到索引的映射
    std::unordered_map<std::string_view, uint16_t> fieldMap_;
};
