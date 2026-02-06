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

#include "core/func.h"
#include "core/impl.h"

#include <span>
#include <string_view>
#include <vector>

using param_t           = std::pair<Type *, bool>; // bool表示是否为可变参数
using param_init_list_t = std::initializer_list<param_t>;
using param_vec_t       = std::vector<param_t>;

// 前向声明
class FunctionType;

// 元信息：参数名、闭包参数名等，仅编译期使用，可使用 STL（参数是否可变属于函数签名，在 Type 内）
class FunctionMetaInfo {
  public:
    static FunctionMetaInfo *
    create(std::vector<std::string> argNames, std::vector<std::string> closureRefs);
    static FunctionMetaInfo *create(
        std::span<const std::string_view> argNames, std::span<const std::string_view> closureRefs);

    size_t argNamesCount() const { return argNames_.size(); }
    std::string_view argNameAt(size_t idx) const;
    std::vector<std::string_view> argNamesSpan() const;
    size_t closureRefsCount() const { return closureRefs_.size(); }
    std::string_view closureRefAt(size_t idx) const;
    std::vector<std::string_view> closureRefsSpan() const;

    void addClosureRef(const std::string &ref);

  private:
    FunctionMetaInfo(std::vector<std::string> argNames, std::vector<std::string> closureRefs);

    std::vector<std::string> argNames_;
    std::vector<std::string> closureRefs_;
};

// 工厂类：用于构造 FunctionType（可以使用 STL）
class FunctionTypeFactory {
    friend class FunctionType;

  public:
    FunctionTypeFactory() = default;

    void setImplMark(ImplMark mark) { implMark_ = mark; }
    void setModifiers(ModifierSet mod) { modifiers_ = mod; }
    void setExitType(Type *type) { exitType_ = type; }

    void addWithArg(const std::string &ident, Type *type, bool isVar) {
        withTypes_.emplace_back(type, isVar);
        if (hasCompileInfo_) {
            argNames_.push_back(ident);
        }
    }

    void addNormArg(const std::string &ident, Type *type, bool isVar) {
        normTypes_.emplace_back(type, isVar);
        if (hasCompileInfo_) {
            argNames_.push_back(ident);
        }
    }

    void addClosureRef(const std::string &ident) {
        if (hasCompileInfo_) {
            closureRefs_.push_back(ident);
        }
    }

    void setHasCompileInfo(bool has) { hasCompileInfo_ = has; }

    // 构建不可变的 FunctionType 对象
    FunctionType *build();

  private:
    ImplMark implMark_     = ImplMark::Graph;
    ModifierSet modifiers_ = Modifier::None;
    std::vector<param_t> withTypes_;
    std::vector<param_t> normTypes_;
    Type *exitType_      = nullptr;
    bool hasCompileInfo_ = true;
    std::vector<std::string> argNames_;
    std::vector<std::string> closureRefs_;
};

// 布局信息：仅用于构造时一次计算，避免重复
struct FunctionTypeLayout {
    size_t totalSize;
    size_t alignedWithTypesSize;
    size_t alignedWithTypeCodesSize;
    size_t alignedWithIsVarSize;
    size_t normBase;
    size_t alignedNormTypesSize;
    size_t alignedNormTypeCodesSize;
    size_t alignedNormIsVarSize;

    // 由 layout 统一计算 data 区各段指针，避免构造处重复偏移计算
    struct DataPtrs {
        Type **withTypes;
        TypeCode *withTypeCodes;
        uint8_t *withIsVar;
        Type **normTypes;
        TypeCode *normTypeCodes;
        uint8_t *normIsVar;
    };
    DataPtrs ptrs(uint8_t *data) const {
        return {
            reinterpret_cast<Type **>(data),
            reinterpret_cast<TypeCode *>(data + alignedWithTypesSize),
            reinterpret_cast<uint8_t *>(data + alignedWithTypesSize + alignedWithTypeCodesSize),
            reinterpret_cast<Type **>(data + normBase),
            reinterpret_cast<TypeCode *>(data + normBase + alignedNormTypesSize),
            reinterpret_cast<uint8_t *>(
                data + normBase + alignedNormTypesSize + alignedNormTypeCodesSize),
        };
    }
};

// 不可变的 FunctionType：类型信息直接嵌入对象
class FunctionType : public CompositeType {
    friend class FunctionTypeFactory;

  public:
    // 禁止直接构造，使用工厂或 create 方法
    FunctionType(const FunctionType &)            = delete;
    FunctionType &operator=(const FunctionType &) = delete;

    // 创建已解析的 FunctionType
    static FunctionType *create();
    static FunctionType *create(
        const param_init_list_t &withTypes, const param_init_list_t &normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None);
    static FunctionType *create(
        const param_vec_t &withTypes, const param_vec_t &normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None);
    static FunctionType *create(
        const param_vec_t &&withTypes, const param_vec_t &&normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None);

    // 从工厂构建
    static FunctionType *fromFactory(FunctionTypeFactory &factory);

    // 从数据直接构建（内部使用，用于 clone 等场景）
    static FunctionType *fromData(
        size_t withCount, size_t normCount, const Type *const *withTypes,
        const TypeCode *withTypeCodes, const bool *withIsVar, const Type *const *normTypes,
        const TypeCode *normTypeCodes, const bool *normIsVar, Type *exitType, ImplMark implMark,
        ModifierSet modifiers, const FunctionMetaInfo *metaInfo);

    /** 编译期就地设置返回类型 */
    void setExitType(Type *type) { exitType_ = type; }
    /** 编译期就地追加闭包引用名 */
    void addClosureRef(const std::string &ref);

    ImplMark implMark() const { return implMark_; }
    ModifierSet modifiers() const { return modifiers_; }
    bool hasModifier(Modifier mod) const { return modifiers_.has(mod); }
    bool checkModifiers() const;

    size_t withTypesCount() const { return withTypes_.size(); }
    size_t normTypesCount() const { return normTypes_.size(); }
    Type *withTypeAt(size_t index) const {
        ASSERT(index < withTypes_.size(), "Index out of range");
        return const_cast<Type *>(withTypes_[index]);
    }
    TypeCode withTypeCodeAt(size_t index) const {
        ASSERT(index < withTypes_.size(), "Index out of range");
        return withTypeCodes_[index];
    }
    bool withIsVarAt(size_t index) const {
        ASSERT(index < withTypes_.size(), "Index out of range");
        return withIsVar_[index] != 0;
    }
    Type *normTypeAt(size_t index) const {
        ASSERT(index < normTypes_.size(), "Index out of range");
        return const_cast<Type *>(normTypes_[index]);
    }
    TypeCode normTypeCodeAt(size_t index) const {
        ASSERT(index < normTypes_.size(), "Index out of range");
        return normTypeCodes_[index];
    }
    bool normIsVarAt(size_t index) const {
        ASSERT(index < normTypes_.size(), "Index out of range");
        return normIsVar_[index] != 0;
    }

    // 批量访问（span），便于范围 for 等
    std::span<Type *const> withTypesSpan() const { return withTypes_; }
    std::span<Type *const> normTypesSpan() const { return normTypes_; }
    std::span<const TypeCode> withTypeCodesSpan() const { return withTypeCodes_; }
    std::span<const TypeCode> normTypeCodesSpan() const { return normTypeCodes_; }

    Type *exitType() const;
    bool hasExitType() const { return exitType_ != nullptr; }

    // 元信息访问（如果存在）
    bool hasMetaInfo() const { return metaInfo_ != nullptr; }
    std::string_view argNameAt(size_t idx) const;
    size_t argNamesCount() const { return metaInfo_ ? metaInfo_->argNamesCount() : 0; }
    size_t closureRefsCount() const { return metaInfo_ ? metaInfo_->closureRefsCount() : 0; }
    std::string_view closureRefAt(size_t idx) const;

    // 兼容性方法：返回编译期信息（按需构造 views，无缓存）
    std::vector<std::string_view> argNames() const;
    std::vector<std::string_view> closureRefs() const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(Type *type) const override;

  private:
    // 根据预计算 layout 一次拷贝（用于 fromData / create / clone）
    FunctionType(
        const FunctionTypeLayout &layout, size_t withCount, size_t normCount,
        const Type *const *withTypes, const TypeCode *withTypeCodes, const bool *withIsVar,
        const Type *const *normTypes, const TypeCode *normTypeCodes, const bool *normIsVar,
        Type *exitType, ImplMark implMark, ModifierSet modifiers, const FunctionMetaInfo *metaInfo);
    // 从工厂直接填充，无临时 vector（一次性使用）
    FunctionType(
        FunctionTypeFactory &factory, const FunctionTypeLayout &layout, Type *exitType,
        ImplMark implMark, ModifierSet modifiers, const FunctionMetaInfo *metaInfo);
    // 从 param_vec 一次遍历填充（用于 create，无临时 vector）
    FunctionType(
        const FunctionTypeLayout &layout, const param_vec_t &withTypes,
        const param_vec_t &normTypes, Type *exitType, ImplMark implMark, ModifierSet modifiers);

    ImplMark implMark_;
    ModifierSet modifiers_;
    mutable Type *exitType_;
    mutable FunctionMetaInfo *metaInfo_ = nullptr;
    // 运行时类型数据（指向 data_[] 内布局）
    std::span<Type *const> withTypes_;
    std::span<const TypeCode> withTypeCodes_;
    std::span<const uint8_t> withIsVar_;
    std::span<Type *const> normTypes_;
    std::span<const TypeCode> normTypeCodes_;
    std::span<const uint8_t> normIsVar_;
    // 灵活数组：仅存储运行时类型数据
    // 内存布局：[withTypes_[count]][withTypeCodes_[count]][withIsVar_[count]]
    //          [normTypes_[count]][normTypeCodes_[count]][normIsVar_[count]]
    uint8_t data_[];
};
