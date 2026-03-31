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
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/composite/func.h"
#include "camel/core/mm.h"
#include "camel/core/mm/alloc/allocator.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"

#include <cstring>

namespace mm = camel::core::mm;
using namespace std;
using namespace camel::core::type;

namespace {

FunctionTypeLayout computeLayout(size_t withCount, size_t normCount) {
    size_t withTypesSize     = withCount * sizeof(Type *);
    size_t withTypeCodesSize = withCount * sizeof(TypeCode);
    size_t withIsVarSize     = withCount * sizeof(uint8_t);
    size_t normTypesSize     = normCount * sizeof(Type *);
    size_t normTypeCodesSize = normCount * sizeof(TypeCode);
    size_t normIsVarSize     = normCount * sizeof(uint8_t);

    size_t aWTypes  = mm::alignUp(withTypesSize, alignof(TypeCode));
    size_t aWCodes  = mm::alignUp(withTypeCodesSize, alignof(uint8_t));
    size_t aWVar    = mm::alignUp(withIsVarSize, alignof(Type *));
    size_t normBase = aWTypes + aWCodes + aWVar;
    size_t aNTypes  = mm::alignUp(normTypesSize, alignof(TypeCode));
    size_t aNCodes  = mm::alignUp(normTypeCodesSize, alignof(uint8_t));
    size_t aNVar    = mm::alignUp(normIsVarSize, alignof(uint8_t));

    size_t dataSize  = normBase + aNTypes + aNCodes + aNVar;
    size_t totalSize = sizeof(FunctionType) + dataSize;

    return FunctionTypeLayout{
        .totalSize                = totalSize,
        .alignedWithTypesSize     = aWTypes,
        .alignedWithTypeCodesSize = aWCodes,
        .alignedWithIsVarSize     = aWVar,
        .normBase                 = normBase,
        .alignedNormTypesSize     = aNTypes,
        .alignedNormTypeCodesSize = aNCodes,
        .alignedNormIsVarSize     = aNVar,
    };
}

} // namespace

// --- FunctionMetaInfo ---

FunctionMetaInfo *
FunctionMetaInfo::create(std::vector<std::string> argNames, std::vector<std::string> closureRefs) {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "FunctionMetaInfo",
        "Allocating FunctionMetaInfo: size: {} bytes",
        sizeof(FunctionMetaInfo)));
    void *mem = mm::permSpace().alloc(sizeof(FunctionMetaInfo), alignof(FunctionMetaInfo));
    ASSERT(mem != nullptr, "Failed to allocate FunctionMetaInfo");
    return new (mem) FunctionMetaInfo(std::move(argNames), std::move(closureRefs));
}

FunctionMetaInfo *FunctionMetaInfo::create(
    std::span<const std::string_view> argNames, std::span<const std::string_view> closureRefs) {
    std::vector<std::string> names(argNames.begin(), argNames.end());
    std::vector<std::string> refs(closureRefs.begin(), closureRefs.end());
    return create(std::move(names), std::move(refs));
}

FunctionMetaInfo::FunctionMetaInfo(
    std::vector<std::string> argNames, std::vector<std::string> closureRefs)
    : argNames_(std::move(argNames)), closureRefs_(std::move(closureRefs)) {}

std::string_view FunctionMetaInfo::argNameAt(size_t idx) const {
    ASSERT(idx < argNames_.size(), "Index out of range");
    return argNames_[idx];
}

std::string_view FunctionMetaInfo::closureRefAt(size_t idx) const {
    ASSERT(idx < closureRefs_.size(), "Index out of range");
    return closureRefs_[idx];
}

std::vector<std::string_view> FunctionMetaInfo::argNamesSpan() const {
    return std::vector<std::string_view>(argNames_.begin(), argNames_.end());
}

std::vector<std::string_view> FunctionMetaInfo::closureRefsSpan() const {
    return std::vector<std::string_view>(closureRefs_.begin(), closureRefs_.end());
}

// --- FunctionTypeFactory ---

FunctionType *FunctionTypeFactory::build() { return FunctionType::fromFactory(*this); }

// --- FunctionType ---

// 从预计算 layout + 指针一次拷贝，不再重复计算布局
FunctionType::FunctionType(
    const FunctionTypeLayout &layout, size_t withCount, size_t normCount,
    const Type *const *withTypes, const TypeCode *withTypeCodes, const bool *withIsVar,
    const Type *const *normTypes, const TypeCode *normTypeCodes, const bool *normIsVar,
    Type *exitType, ImplMark implMark, ModifierSet modifiers, const FunctionMetaInfo *metaInfo)
    : CompositeType(TypeCode::Function), implMark_(implMark), modifiers_(modifiers),
      exitType_(exitType), metaInfo_(const_cast<FunctionMetaInfo *>(metaInfo)) {
    auto p         = layout.ptrs(data_);
    withTypes_     = std::span(p.withTypes, withCount);
    withTypeCodes_ = std::span(p.withTypeCodes, withCount);
    withIsVar_     = std::span(p.withIsVar, withCount);
    normTypes_     = std::span(p.normTypes, normCount);
    normTypeCodes_ = std::span(p.normTypeCodes, normCount);
    normIsVar_     = std::span(p.normIsVar, normCount);

    for (size_t i = 0; i < withCount; ++i) {
        p.withTypes[i]     = const_cast<Type *>(withTypes[i]);
        p.withTypeCodes[i] = withTypeCodes[i];
        p.withIsVar[i]     = withIsVar[i] ? 1 : 0;
    }
    for (size_t i = 0; i < normCount; ++i) {
        p.normTypes[i]     = const_cast<Type *>(normTypes[i]);
        p.normTypeCodes[i] = normTypeCodes[i];
        p.normIsVar[i]     = normIsVar[i] ? 1 : 0;
    }
}

// 从工厂直接写入 data_，一次遍历，无临时 vector
FunctionType::FunctionType(
    FunctionTypeFactory &factory, const FunctionTypeLayout &layout, Type *exitType,
    ImplMark implMark, ModifierSet modifiers, const FunctionMetaInfo *metaInfo)
    : CompositeType(TypeCode::Function), implMark_(implMark), modifiers_(modifiers),
      exitType_(exitType), metaInfo_(const_cast<FunctionMetaInfo *>(metaInfo)) {
    const size_t withCount = factory.withTypes_.size();
    const size_t normCount = factory.normTypes_.size();
    auto p                 = layout.ptrs(data_);

    withTypes_     = std::span(p.withTypes, withCount);
    withTypeCodes_ = std::span(p.withTypeCodes, withCount);
    withIsVar_     = std::span(p.withIsVar, withCount);
    normTypes_     = std::span(p.normTypes, normCount);
    normTypeCodes_ = std::span(p.normTypeCodes, normCount);
    normIsVar_     = std::span(p.normIsVar, normCount);

    for (size_t i = 0; i < withCount; ++i) {
        const auto &[type, isVar] = factory.withTypes_[i];
        p.withTypes[i]            = type;
        p.withTypeCodes[i]        = type->code();
        p.withIsVar[i]            = isVar ? 1 : 0;
    }
    for (size_t i = 0; i < normCount; ++i) {
        const auto &[type, isVar] = factory.normTypes_[i];
        p.normTypes[i]            = type;
        p.normTypeCodes[i]        = type->code();
        p.normIsVar[i]            = isVar ? 1 : 0;
    }
}

// 从 param_vec 一次遍历写入 data_，无临时 vector
FunctionType::FunctionType(
    const FunctionTypeLayout &layout, const param_vec_t &withTypes, const param_vec_t &normTypes,
    Type *exitType, ImplMark implMark, ModifierSet modifiers)
    : CompositeType(TypeCode::Function), implMark_(implMark), modifiers_(modifiers),
      exitType_(exitType), metaInfo_(nullptr) {
    const size_t withCount = withTypes.size();
    const size_t normCount = normTypes.size();
    auto p                 = layout.ptrs(data_);

    withTypes_     = std::span(p.withTypes, withCount);
    withTypeCodes_ = std::span(p.withTypeCodes, withCount);
    withIsVar_     = std::span(p.withIsVar, withCount);
    normTypes_     = std::span(p.normTypes, normCount);
    normTypeCodes_ = std::span(p.normTypeCodes, normCount);
    normIsVar_     = std::span(p.normIsVar, normCount);

    for (size_t i = 0; i < withCount; ++i) {
        const auto &[type, isVar] = withTypes[i];
        p.withTypes[i]            = type;
        p.withTypeCodes[i]        = type->code();
        p.withIsVar[i]            = isVar ? 1 : 0;
    }
    for (size_t i = 0; i < normCount; ++i) {
        const auto &[type, isVar] = normTypes[i];
        p.normTypes[i]            = type;
        p.normTypeCodes[i]        = type->code();
        p.normIsVar[i]            = isVar ? 1 : 0;
    }
}

Type *FunctionType::exitType() const {
    // 如果没有返回值类型，默认为void
    // 但此时返回值仍然是未设置状态，以便编译器进行类型推导
    return exitType_ ? exitType_ : Type::Void();
}

bool FunctionType::checkModifiers() const { return true; }

std::string_view FunctionType::argNameAt(size_t idx) const {
    ASSERT(metaInfo_ != nullptr, "No meta info available");
    return metaInfo_->argNameAt(idx);
}

std::string_view FunctionType::closureRefAt(size_t index) const {
    ASSERT(metaInfo_ != nullptr, "No meta info available");
    return metaInfo_->closureRefAt(index);
}

std::vector<std::string_view> FunctionType::argNames() const {
    return metaInfo_ ? metaInfo_->argNamesSpan() : std::vector<std::string_view>();
}

std::vector<std::string_view> FunctionType::closureRefs() const {
    return metaInfo_ ? metaInfo_->closureRefsSpan() : std::vector<std::string_view>();
}

Type *FunctionType::resolve(const type_vec_t &typeList) const {
    ASSERT(false, "Not implemented");
    return nullptr;
}

bool FunctionType::resolved() const { return true; }

string FunctionType::toString() const {
    string result;
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    if (!modifiers_.empty()) {
        result += string(modifiers_) + " ";
    }
    if (!withTypes_.empty()) {
        result += "<";
        for (size_t i = 0; i < withTypes_.size(); i++) {
            if (i > 0)
                result += ", ";
            if (withIsVarAt(i))
                result += "var ";
            if (metaInfo_)
                result += string(argNameAt(i)) + ": ";
            result += withTypes_[i]->toString();
        }
        result += "> ";
    }
    result += "(";
    for (size_t i = 0; i < normTypes_.size(); i++) {
        if (i > 0)
            result += ", ";
        if (normIsVarAt(i))
            result += "var ";
        if (metaInfo_) {
            size_t idx = i + withTypes_.size();
            result += string(argNameAt(idx)) + ": ";
        }
        result += normTypes_[i]->toString();
    }
    result += ") => ";
    result += exitType_ ? exitType_->toString() : "<null>";
    return result;
}

string FunctionType::mangle() const {
    string result = "F";
    if (!withTypes_.empty()) {
        result += "W" + to_string(withTypes_.size());
        for (size_t i = 0; i < withTypes_.size(); i++) {
            if (withIsVarAt(i))
                result += "V";
            result += withTypes_[i]->mangle();
        }
    }
    if (!normTypes_.empty()) {
        result += "N" + to_string(normTypes_.size());
        for (size_t i = 0; i < normTypes_.size(); i++) {
            if (normIsVarAt(i))
                result += "V";
            result += normTypes_[i]->mangle();
        }
    }
    result += "R";
    result += exitType_ ? exitType_->mangle() : "v";
    return result;
}

FunctionType *FunctionType::create() {
    // 空 FunctionType，使用工厂构建
    FunctionTypeFactory factory;
    factory.setHasCompileInfo(true);
    return factory.build();
}

FunctionType *FunctionType::create(
    const param_init_list_t &withTypes, const param_init_list_t &normTypes, Type *returnType,
    const ModifierSet &modifiers) {
    param_vec_t withVec(withTypes);
    param_vec_t normVec(normTypes);
    return create(withVec, normVec, returnType, modifiers);
}

FunctionType *FunctionType::create(
    const param_vec_t &withTypes, const param_vec_t &normTypes, Type *returnType,
    const ModifierSet &modifiers) {
    FunctionTypeLayout layout = computeLayout(withTypes.size(), normTypes.size());
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "FunctionType",
        "Allocating FunctionType: withCount={}, normCount={}, totalSize: {} bytes",
        withTypes.size(),
        normTypes.size(),
        layout.totalSize));
    void *mem = mm::permSpace().alloc(layout.totalSize, alignof(FunctionType));
    ASSERT(mem != nullptr, "Failed to allocate FunctionType from permSpace");
    return new (mem)
        FunctionType(layout, withTypes, normTypes, returnType, ImplMark::Graph, modifiers);
}

FunctionType *FunctionType::create(
    const param_vec_t &&withTypes, const param_vec_t &&normTypes, Type *returnType,
    const ModifierSet &modifiers) {
    return create(withTypes, normTypes, returnType, modifiers);
}

FunctionType *FunctionType::fromFactory(FunctionTypeFactory &factory) {
    const size_t withCount    = factory.withTypes_.size();
    const size_t normCount    = factory.normTypes_.size();
    FunctionTypeLayout layout = computeLayout(withCount, normCount);

    const FunctionMetaInfo *metaInfo = nullptr;
    if (factory.hasCompileInfo_) {
        metaInfo =
            FunctionMetaInfo::create(std::move(factory.argNames_), std::move(factory.closureRefs_));
    }

    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "FunctionType",
        "Allocating FunctionType(fromFactory): withCount={}, normCount={}, "
        "totalSize: {} bytes",
        withCount,
        normCount,
        layout.totalSize));
    void *mem = mm::permSpace().alloc(layout.totalSize, alignof(FunctionType));
    ASSERT(mem != nullptr, "Failed to allocate FunctionType from permSpace");
    return new (mem) FunctionType(
        factory,
        layout,
        factory.exitType_,
        factory.implMark_,
        factory.modifiers_,
        metaInfo);
}

FunctionType *FunctionType::fromData(
    size_t withCount, size_t normCount, const Type *const *withTypes, const TypeCode *withTypeCodes,
    const bool *withIsVar, const Type *const *normTypes, const TypeCode *normTypeCodes,
    const bool *normIsVar, Type *exitType, ImplMark implMark, ModifierSet modifiers,
    const FunctionMetaInfo *metaInfo) {
    FunctionTypeLayout layout = computeLayout(withCount, normCount);
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "FunctionType",
        "Allocating FunctionType(fromData): withCount={}, normCount={}, "
        "totalSize: {} bytes",
        withCount,
        normCount,
        layout.totalSize));
    void *mem = mm::permSpace().alloc(layout.totalSize, alignof(FunctionType));
    ASSERT(mem != nullptr, "Failed to allocate FunctionType from permSpace");
    return new (mem) FunctionType(
        layout,
        withCount,
        normCount,
        withTypes,
        withTypeCodes,
        withIsVar,
        normTypes,
        normTypeCodes,
        normIsVar,
        exitType,
        implMark,
        modifiers,
        metaInfo);
}

void FunctionMetaInfo::addClosureRef(const std::string &ref) { closureRefs_.push_back(ref); }

void FunctionType::addClosureRef(const std::string &ref) {
    if (metaInfo_)
        metaInfo_->addClosureRef(ref);
}

Type *FunctionType::clone(bool deep /* = false */) const {
    // 准备数据
    std::vector<Type *> withTypePtrs;
    std::vector<TypeCode> withTypeCodes;
    std::vector<uint8_t> withIsVar;
    std::vector<Type *> normTypePtrs;
    std::vector<TypeCode> normTypeCodes;
    std::vector<uint8_t> normIsVar;

    withTypePtrs.reserve(withTypes_.size());
    withTypeCodes.reserve(withTypes_.size());
    withIsVar.reserve(withTypes_.size());
    for (size_t i = 0; i < withTypes_.size(); ++i) {
        withTypePtrs.push_back(deep ? withTypes_[i]->clone(true) : withTypes_[i]);
        withTypeCodes.push_back(withTypeCodes_[i]);
        withIsVar.push_back(withIsVar_[i]);
    }

    normTypePtrs.reserve(normTypes_.size());
    normTypeCodes.reserve(normTypes_.size());
    normIsVar.reserve(normTypes_.size());
    for (size_t i = 0; i < normTypes_.size(); ++i) {
        normTypePtrs.push_back(deep ? normTypes_[i]->clone(true) : normTypes_[i]);
        normTypeCodes.push_back(normTypeCodes_[i]);
        normIsVar.push_back(normIsVar_[i]);
    }

    Type *newExitType = deep && exitType_ ? exitType_->clone(true) : exitType_;

    const FunctionMetaInfo *newMetaInfo = nullptr;
    if (metaInfo_) {
        auto an = argNames(), cr = closureRefs();
        newMetaInfo = FunctionMetaInfo::create(
            std::span<const std::string_view>(an),
            std::span<const std::string_view>(cr));
    }

    return fromData(
        withTypes_.size(),
        normTypes_.size(),
        withTypePtrs.data(),
        withTypeCodes.data(),
        reinterpret_cast<const bool *>(withIsVar.data()),
        normTypePtrs.data(),
        normTypeCodes.data(),
        reinterpret_cast<const bool *>(normIsVar.data()),
        newExitType,
        implMark_,
        modifiers_,
        newMetaInfo);
}

bool FunctionType::equals(Type *other) const {
    if (this == other)
        return true;
    if (!other || other->code() != TypeCode::Function)
        return false;

    const FunctionType &otherFunc = static_cast<const FunctionType &>(*other);

    if (withTypes_.size() != otherFunc.withTypes_.size() ||
        normTypes_.size() != otherFunc.normTypes_.size())
        return false;

    for (size_t i = 0; i < withTypes_.size(); i++) {
        if (withIsVar_[i] != otherFunc.withIsVar_[i] ||
            !withTypes_[i]->equals(otherFunc.withTypes_[i]))
            return false;
    }
    for (size_t i = 0; i < normTypes_.size(); i++) {
        if (normIsVar_[i] != otherFunc.normIsVar_[i] ||
            !normTypes_[i]->equals(otherFunc.normTypes_[i]))
            return false;
    }
    if (exitType_ && otherFunc.exitType_) {
        if (!exitType_->equals(otherFunc.exitType_))
            return false;
    } else if (exitType_ || otherFunc.exitType_) {
        return false;
    }
    return true;
}

CastSafety FunctionType::castSafetyFrom(Type *sourceType) const {
    if (auto r = Type::checkCastSafetyWithAny(code(), sourceType))
        return *r;
    if (this == sourceType)
        return CastSafety::Safe;
    return CastSafety::Forbidden;
}

bool FunctionType::assignableFrom(Type *other) const {
    if (this == other)
        return true;
    if (!other || other->code() != TypeCode::Function)
        return false;

    const FunctionType &otherFunc = static_cast<const FunctionType &>(*other);

    if (withTypes_.size() != otherFunc.withTypes_.size() ||
        normTypes_.size() != otherFunc.normTypes_.size())
        return false;

    for (size_t i = 0; i < withTypes_.size(); i++) {
        if (withIsVar_[i] != otherFunc.withIsVar_[i] ||
            !otherFunc.withTypes_[i]->assignableFrom(withTypes_[i]))
            return false;
    }

    for (size_t i = 0; i < normTypes_.size(); i++) {
        if (normIsVar_[i] != otherFunc.normIsVar_[i] ||
            !otherFunc.normTypes_[i]->assignableFrom(normTypes_[i]))
            return false;
    }

    if (exitType_ && otherFunc.exitType_) {
        if (!exitType_->assignableFrom(otherFunc.exitType_))
            return false;
    } else if (exitType_ || otherFunc.exitType_) {
        return false;
    }
    return true;
}
