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

#include "composite.h"

#include <vector>

// 前向声明
class ArrayType;

// 工厂类：用于构造 ArrayType（可以使用 STL）
class ArrayTypeFactory {
    friend class ArrayType;

  public:
    ArrayTypeFactory() = default;
    explicit ArrayTypeFactory(Type *elemType) : elemType_(elemType) {}

    void setElemType(Type *elemType) { elemType_ = elemType; }
    void addRef(size_t index) { refs_.push_back(index); }
    void setRefs(const std::vector<size_t> &refs) { refs_ = refs; }

    // 构建不可变的 ArrayType 对象
    ArrayType *build();

  private:
    Type *elemType_ = nullptr;
    std::vector<size_t> refs_;
};

// 不可变的 ArrayType：类型信息直接嵌入对象
class ArrayType : public CompositeType {
    friend class ArrayTypeFactory;

  public:
    // 禁止直接构造，使用工厂或 create 方法
    ArrayType(const ArrayType &)            = delete;
    ArrayType &operator=(const ArrayType &) = delete;

    // 创建已解析的 ArrayType（无 refs）
    static ArrayType *create(Type *elemType = nullptr);

    // 从工厂构建
    static ArrayType *fromFactory(ArrayTypeFactory &factory);

    // 从数据直接构建（内部使用，用于 clone 等场景）
    static ArrayType *fromData(Type *elemType, size_t refCount, const size_t *refs);

    Type *elemType() const { return elemType_; }
    TypeCode elemTypeCode() const { return elemTypeCode_; }
    size_t refCount() const { return refCount_; }
    const size_t *refs() const { return refs_; } // 返回指向内部数组的指针

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override;
    virtual bool assignableFrom(Type *sourceType) const override;

  private:
    // 私有构造函数：由工厂或 create 调用
    ArrayType(Type *elemType, size_t refCount, const size_t *refs);

    Type *elemType_;        // 元素类型指针（用于类型系统）
    TypeCode elemTypeCode_; // 元素类型代码（用于运行时）
    size_t refCount_;       // 引用索引数量
    size_t refs_[];         // 灵活数组：引用索引列表
};
