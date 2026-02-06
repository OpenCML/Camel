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
 * Created: Oct. 06, 2024
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"
#include "utils/assert.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

// 前向声明
class StructType;

// 布局信息：一次计算，避免重复
struct StructTypeLayout {
    size_t totalSize;
    size_t size;
    size_t refCount;
    size_t alignedTypesSize;
    size_t alignedTypeCodesSize;
    size_t alignedNameOffsetsSize;
    size_t alignedRefsSize;
    size_t fieldNamesDataSize;

    struct DataPtrs {
        Type **types;
        TypeCode *typeCodes;
        size_t *nameOffsets;
        size_t *refs;
        char *fieldNamesBase;
    };
    DataPtrs ptrs(uint8_t *data) const {
        size_t off = 0;
        Type **t   = reinterpret_cast<Type **>(data + off);
        off += alignedTypesSize;
        TypeCode *tc = reinterpret_cast<TypeCode *>(data + off);
        off += alignedTypeCodesSize;
        size_t *no = reinterpret_cast<size_t *>(data + off);
        off += alignedNameOffsetsSize;
        size_t *r = reinterpret_cast<size_t *>(data + off);
        off += alignedRefsSize;
        char *fn = reinterpret_cast<char *>(data + off);
        return {t, tc, no, r, fn};
    }
};

// 工厂类：用于构造 StructType（可以使用 STL）
class StructTypeFactory {
    friend class StructType;

  public:
    StructTypeFactory() = default;

    bool add(const std::string &name, Type *type) {
        if (has(name)) {
            return false;
        }
        fields_.emplace_back(name, type);
        if (type && type->code() == TypeCode::Ref) {
            refs_.push_back(name);
        }
        return true;
    }

    bool has(const std::string &name) const {
        return std::any_of(fields_.begin(), fields_.end(), [&](const auto &p) {
            return p.first == name;
        });
    }

    // 构建不可变的 StructType 对象
    StructType *build();

  private:
    std::vector<std::pair<std::string, Type *>> fields_;
    std::vector<std::string> refs_;
};

// 不可变的 StructType：类型信息直接嵌入对象
class StructType : public CompositeType {
    friend class StructTypeFactory;

  public:
    // 禁止直接构造，使用工厂或 create 方法
    StructType(const StructType &)            = delete;
    StructType &operator=(const StructType &) = delete;

    // 创建已解析的 StructType
    static StructType *create();

    // 从工厂构建
    static StructType *fromFactory(StructTypeFactory &factory);

    size_t size() const { return size_; }
    Type *typeAt(size_t index) const {
        ASSERT(index < size_, "StructType: index out of range");
        return const_cast<Type *>(typesPtr_[index]);
    }
    TypeCode codeAt(size_t index) const {
        ASSERT(index < size_, "StructType: index out of range");
        return typeCodesPtr_[index];
    }
    std::span<Type *const> types() const { return std::span<Type *const>(typesPtr_, size_); }
    std::span<const TypeCode> codes() const {
        return std::span<const TypeCode>(typeCodesPtr_, size_);
    }
    std::string_view fieldName(size_t index) const {
        ASSERT(index < size_, "StructType: index out of range");
        return fieldNameAt(index);
    }
    std::optional<size_t> findField(std::string_view name) const;
    size_t refCount() const { return refCount_; }
    const size_t *refs() const { return refsPtr(); }

    Type *operator|(const StructType &other) const;
    Type *operator&(const StructType &other) const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(Type *type) const override;

  private:
    // 从预计算 layout + 数据一次拷贝
    StructType(
        const StructTypeLayout &layout, const Type *const *types, const TypeCode *typeCodes,
        const size_t *nameOffsets, const char *fieldNamesData, const size_t *refs);

    // 辅助方法：从数据构建（内部使用）
    static StructType *fromFactoryData(
        const std::vector<std::pair<std::string, Type *>> &fields,
        const std::vector<std::string> &refs);

    size_t size_;                   // 字段数量
    size_t refCount_;               // 引用索引数量
    Type **typesPtr_;               // 指向 types 数组的指针（构造时计算）
    TypeCode *typeCodesPtr_;        // 指向 typeCodes 数组的指针（构造时计算）
    size_t *nameOffsetsPtr_;        // 指向 nameOffsets 数组的指针（构造时计算）
    size_t *refsPtr_;               // 指向 refs 数组的指针（构造时计算）
    const char *fieldNamesBasePtr_; // 指向 fieldNames 字符串数据的指针（构造时计算）
    // 灵活数组：存储 types_[size_], typeCodes_[size_], nameOffsets_[size_], refs_[refCount_],
    // fieldNames字符串数据 内存布局：[StructType
    // base][size_][refCount_][typesPtr_][typeCodesPtr_][nameOffsetsPtr_][refsPtr_][fieldNamesBasePtr_][types_[size_]][typeCodes_[size_]][nameOffsets_[size_]][refs_[refCount_]][fieldNames字符串数据]
    uint8_t data_[]; // 灵活数组：存储所有数据

    // 访问方法（现在直接返回存储的指针）
    const Type **typesPtr() const { return const_cast<const Type **>(typesPtr_); }
    const TypeCode *typeCodesPtr() const { return typeCodesPtr_; }
    const size_t *nameOffsetsPtr() const { return nameOffsetsPtr_; }
    const size_t *refsPtr() const { return refsPtr_; }
    const char *fieldNamesBasePtr() const { return fieldNamesBasePtr_; }
    // 通过偏移量获取字段名称
    std::string_view fieldNameAt(size_t index) const {
        size_t offset = nameOffsetsPtr_[index];
        if (index + 1 < size_) {
            // 下一个字段的偏移量减去当前偏移量，再减1（去掉null terminator）
            size_t length = nameOffsetsPtr_[index + 1] - offset - 1;
            return std::string_view(fieldNamesBasePtr_ + offset, length);
        } else {
            // 最后一个字段：从偏移量开始到字符串结束
            return std::string_view(fieldNamesBasePtr_ + offset);
        }
    }
};
