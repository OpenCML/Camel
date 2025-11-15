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
 * Updated: Nov. 14, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/memory/gc.h"
#include "core/type/type.h"

using Int    = int32_t;
using Long   = int64_t;
using Float  = float;
using Double = double;
using Bool   = bool;
using Byte   = char;

struct LayoutInfo {
    size_t size;
    size_t align;
};

LayoutInfo getLayoutInfoOfType(TypeCode code);

inline size_t alignUp(size_t n, size_t align) { return (n + align - 1) & ~(align - 1); }

class DataLayout {
  public:
    DataLayout(TypeCode code) : code_(code) {
        LayoutInfo info = getLayoutInfoOfType(code_);
        size_           = info.size;
        align_          = info.align;
    }

    TypeCode code() const { return code_; }
    uint16_t size() const { return size_; }
    uint16_t align() const { return align_; }

  protected:
    TypeCode code_;
    uint16_t size_;  // 总大小
    uint16_t align_; // 对齐值
};

static_assert(sizeof(DataLayout) == 8, "DataLayout size must be 8 bytes");

class CompositeDataLayout : public DataLayout {
  public:
    static CompositeDataLayout *
    create(IAllocator *allocator, TypeCode self, const std::vector<DataLayout> &elements);

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

  private:
    CompositeDataLayout(TypeCode self, uint16_t elemCnt)
        : DataLayout(self), elemCnt_(elemCnt), offsets_(nullptr), types_(nullptr) {}

    void build(const std::vector<DataLayout> &elements);

    uint16_t elemCnt_;
    uint16_t *offsets_;
    TypeCode *types_;
};
