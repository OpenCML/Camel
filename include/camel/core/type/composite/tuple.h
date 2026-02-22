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
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/utils/assert.h"
#include "composite.h"

#include <span>
#include <vector>

// 前向声明
class TupleType;

// 布局信息：一次计算，避免重复
struct TupleTypeLayout {
    size_t totalSize;
    size_t size;
    size_t refCount;
    size_t alignedTypesSize;
    size_t alignedTypeCodesSize;

    struct DataPtrs {
        Type **types;
        TypeCode *typeCodes;
        size_t *refs;
    };
    DataPtrs ptrs(uint8_t *data) const {
        return {
            reinterpret_cast<Type **>(data),
            reinterpret_cast<TypeCode *>(data + alignedTypesSize),
            reinterpret_cast<size_t *>(data + alignedTypesSize + alignedTypeCodesSize),
        };
    }
};

// 工厂类：用于构造 TupleType（可以使用 STL）
class TupleTypeFactory {
    friend class TupleType;

  public:
    TupleTypeFactory() = default;
    explicit TupleTypeFactory(const std::vector<Type *> &types) {
        for (auto *type : types) {
            add(type);
        }
    }

    void add(Type *type) { types_.push_back(type); }
    void set(size_t index, Type *type) {
        if (index >= types_.size()) {
            types_.resize(index + 1, nullptr);
        }
        types_[index] = type;
    }

    // 构建不可变的 TupleType 对象
    TupleType *build();

  private:
    std::vector<Type *> types_;
};

// 不可变的 TupleType：类型信息直接嵌入对象
class TupleType : public CompositeType {
    friend class TupleTypeFactory;

  public:
    // 禁止直接构造，使用工厂或 create 方法
    TupleType(const TupleType &)            = delete;
    TupleType &operator=(const TupleType &) = delete;

    // 创建已解析的 TupleType
    static TupleType *create();
    static TupleType *create(const std::vector<Type *> &types);
    static TupleType *create(std::vector<Type *> &&types);

    // 从工厂构建
    static TupleType *fromFactory(TupleTypeFactory &factory);

    // 辅助方法：从数据构建（内部使用）
    static TupleType *fromFactoryData(
        const Type *const *types, const TypeCode *typeCodes, const size_t *refs, size_t size,
        size_t refCount);

    size_t size() const { return size_; }
    Type *typeAt(size_t index) const {
        ASSERT(index < size_, "TupleType: index out of range");
        return const_cast<Type *>(typesPtr_[index]);
    }
    TypeCode codeAt(size_t index) const {
        ASSERT(index < size_, "TupleType: index out of range");
        return typeCodesPtr_[index];
    }
    std::span<Type *const> types() const { return std::span<Type *const>(typesPtr_, size_); }
    std::span<const TypeCode> codes() const {
        return std::span<const TypeCode>(typeCodesPtr_, size_);
    }
    size_t refCount() const { return refCount_; }
    const size_t *refs() const { return refsPtr(); } // 返回指向内部数组的指针

    TupleType *slice(size_t start, size_t end) const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override;
    virtual bool assignableFrom(Type *sourceType) const override;

  private:
    // 从预计算 layout + 数据指针一次拷贝
    TupleType(
        const TupleTypeLayout &layout, const Type *const *types, const TypeCode *typeCodes,
        const size_t *refs);
    // 从工厂直接填充，无临时 vector
    TupleType(TupleTypeFactory &factory, const TupleTypeLayout &layout);
    // 从 vector<Type*> 一次遍历填充（用于 create）
    TupleType(const TupleTypeLayout &layout, const std::vector<Type *> &types);

    size_t size_;            // 元素数量
    size_t refCount_;        // 引用索引数量
    Type **typesPtr_;        // 指向 types 数组的指针（构造时计算）
    TypeCode *typeCodesPtr_; // 指向 typeCodes 数组的指针（构造时计算）
    size_t *refsPtr_;        // 指向 refs 数组的指针（构造时计算）
    // 灵活数组：存储 types_[size_], typeCodes_[size_], refs_[refCount_]
    // 内存布局：[TupleType
    // base][size_][refCount_][typesPtr_][typeCodesPtr_][refsPtr_][types_[size_]][typeCodes_[size_]][refs_[refCount_]]
    uint8_t data_[]; // 灵活数组：存储所有数据

    // 访问方法（现在直接返回存储的指针）
    const Type **typesPtr() const { return const_cast<const Type **>(typesPtr_); }
    const TypeCode *typeCodesPtr() const { return typeCodesPtr_; }
    const size_t *refsPtr() const { return refsPtr_; }
};
