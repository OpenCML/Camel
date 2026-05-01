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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/func.h"
#include "camel/core/impl.h"
#include "camel/core/type/composite/composite.h"

#include <span>
#include <string_view>
#include <vector>

namespace camel::core::type {

using param_t = std::pair<Type *, bool>; // bool indicates whether the parameter is variadic.
using param_init_list_t = std::initializer_list<param_t>;
using param_vec_t       = std::vector<param_t>;

// Forward declaration.
class FunctionType;

// Metadata: parameter names, closure parameter names, etc. Compile-time only; STL is fine here
// because variadic-ness belongs to the function signature inside Type.
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

// Factory for constructing FunctionType (may use STL).
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

    // Build an immutable FunctionType object.
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

// Layout information: computed once during construction to avoid repeated work.
struct FunctionTypeLayout {
    size_t totalSize;
    size_t alignedWithTypesSize;
    size_t alignedWithTypeCodesSize;
    size_t alignedWithIsVarSize;
    size_t normBase;
    size_t alignedNormTypesSize;
    size_t alignedNormTypeCodesSize;
    size_t alignedNormIsVarSize;

    // Let layout compute the pointers to each section of the data area, avoiding repeated offset
    // math.
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

// Immutable FunctionType: type information is embedded directly in the object.
class FunctionType : public CompositeType {
    friend class FunctionTypeFactory;

  public:
    // Direct construction is forbidden; use the factory or create methods.
    FunctionType(const FunctionType &)            = delete;
    FunctionType &operator=(const FunctionType &) = delete;

    // Create a resolved FunctionType.
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

    // Build from a factory.
    static FunctionType *fromFactory(FunctionTypeFactory &factory);

    // Build directly from data (internal use, e.g. clone).
    static FunctionType *fromData(
        size_t withCount, size_t normCount, const Type *const *withTypes,
        const TypeCode *withTypeCodes, const bool *withIsVar, const Type *const *normTypes,
        const TypeCode *normTypeCodes, const bool *normIsVar, Type *exitType, ImplMark implMark,
        ModifierSet modifiers, const FunctionMetaInfo *metaInfo);

    /** Set the return type in place at compile time. */
    void setExitType(Type *type) { exitType_ = type; }
    /** Append a closure reference name in place at compile time. */
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

    // Batch access (span), convenient for range-for and similar use.
    std::span<Type *const> withTypesSpan() const { return withTypes_; }
    std::span<Type *const> normTypesSpan() const { return normTypes_; }
    std::span<const TypeCode> withTypeCodesSpan() const { return withTypeCodes_; }
    std::span<const TypeCode> normTypeCodesSpan() const { return normTypeCodes_; }

    Type *exitType() const;
    bool hasExitType() const { return exitType_ != nullptr; }

    // Metadata access (if present).
    bool hasMetaInfo() const { return metaInfo_ != nullptr; }
    std::string_view argNameAt(size_t idx) const;
    size_t argNamesCount() const { return metaInfo_ ? metaInfo_->argNamesCount() : 0; }
    size_t closureRefsCount() const { return metaInfo_ ? metaInfo_->closureRefsCount() : 0; }
    std::string_view closureRefAt(size_t idx) const;

    // Compatibility helpers: return compile-time info by constructing views on demand, with no
    // cache.
    std::vector<std::string_view> argNames() const;
    std::vector<std::string_view> closureRefs() const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override;
    virtual bool assignableFrom(Type *sourceType) const override;

  private:
    // Copy once from the precomputed layout (used by fromData / create / clone).
    FunctionType(
        const FunctionTypeLayout &layout, size_t withCount, size_t normCount,
        const Type *const *withTypes, const TypeCode *withTypeCodes, const bool *withIsVar,
        const Type *const *normTypes, const TypeCode *normTypeCodes, const bool *normIsVar,
        Type *exitType, ImplMark implMark, ModifierSet modifiers, const FunctionMetaInfo *metaInfo);
    // Fill directly from the factory, with no temporary vector (one-shot use).
    FunctionType(
        FunctionTypeFactory &factory, const FunctionTypeLayout &layout, Type *exitType,
        ImplMark implMark, ModifierSet modifiers, const FunctionMetaInfo *metaInfo);
    // Fill in one pass from param_vec (used by create, with no temporary vector).
    FunctionType(
        const FunctionTypeLayout &layout, const param_vec_t &withTypes,
        const param_vec_t &normTypes, Type *exitType, ImplMark implMark, ModifierSet modifiers);

    ImplMark implMark_;
    ModifierSet modifiers_;
    mutable Type *exitType_;
    mutable FunctionMetaInfo *metaInfo_ = nullptr;
    // Runtime type data (points into the data_[] layout).
    std::span<Type *const> withTypes_;
    std::span<const TypeCode> withTypeCodes_;
    std::span<const uint8_t> withIsVar_;
    std::span<Type *const> normTypes_;
    std::span<const TypeCode> normTypeCodes_;
    std::span<const uint8_t> normIsVar_;
    // Flexible array: stores runtime type data only
    // Memory layout: [withTypes_[count]][withTypeCodes_[count]][withIsVar_[count]]
    //           [normTypes_[count]][normTypeCodes_[count]][normIsVar_[count]]
    uint8_t data_[];
};

} // namespace camel::core::type
