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
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/composite/array.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"

using namespace std;

ArrayType *ArrayTypeFactory::build() {
    if (!elemType_) {
        elemType_ = Type::Void();
    }
    return ArrayType::fromFactory(*this);
}

ArrayType::ArrayType(Type *elemType, size_t refCount, const size_t *refs)
    : CompositeType(TypeCode::Array), elemType_(elemType), elemTypeCode_(elemType->code()),
      refCount_(refCount) {
    // 复制 refs 到灵活数组
    for (size_t i = 0; i < refCount; ++i) {
        refs_[i] = refs[i];
    }
}

ArrayType *ArrayType::create(Type *elemType) {
    if (!elemType) {
        elemType = Type::Void();
    }
    // 计算所需内存大小
    size_t baseSize  = sizeof(ArrayType);
    size_t totalSize = baseSize; // 无 refs 时，refCount_ = 0

    EXEC_WHEN_DEBUG(
        l.in("ArrayType")
            .debug("Allocating ArrayType: {}[], size: {} bytes", elemType->toString(), totalSize));

    void *mem = mm::permSpace().alloc(totalSize, alignof(ArrayType));
    ASSERT(mem != nullptr, "Failed to allocate ArrayType from permSpace");
    return new (mem) ArrayType(elemType, 0, nullptr);
}

ArrayType *ArrayType::fromFactory(ArrayTypeFactory &factory) {
    if (!factory.elemType_) {
        factory.elemType_ = Type::Void();
    }
    return fromData(factory.elemType_, factory.refs_.size(), factory.refs_.data());
}

ArrayType *ArrayType::fromData(Type *elemType, size_t refCount, const size_t *refs) {
    if (!elemType) {
        elemType = Type::Void();
    }

    // 计算所需内存大小：基础大小 + refs 数组
    size_t baseSize  = sizeof(ArrayType);
    size_t refsSize  = refCount * sizeof(size_t);
    size_t totalSize = baseSize + refsSize;

    EXEC_WHEN_DEBUG(
        l.in("ArrayType")
            .debug("Allocating ArrayType: {}[], size: {} bytes", elemType->toString(), totalSize));

    void *mem = mm::permSpace().alloc(totalSize, alignof(ArrayType));
    ASSERT(mem != nullptr, "Failed to allocate ArrayType from permSpace");

    const size_t *refsPtr = refCount > 0 ? refs : nullptr;
    return new (mem) ArrayType(elemType, refCount, refsPtr);
}

Type *ArrayType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "ArrayType is already resolved");

    ASSERT(
        typeList.size() == refCount_,
        "Type list size does not match the number of references in ArrayType");

    Type *newElemType = elemType_;
    for (const auto &type : typeList) {
        if (newElemType->code() == TypeCode::Void || newElemType->code() == TypeCode::Any ||
            newElemType->code() == TypeCode::Ref) {
            newElemType = type;
        } else if (!newElemType->equals(type)) {
            throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
                .commit("Array", type->toString(), newElemType->toString());
        }
    }
    auto newArray = ArrayType::create(newElemType);
    ASSERT(newArray->resolved(), "ArrayType is not fully resolved");
    return newArray;
}

bool ArrayType::resolved() const { return refCount_ == 0; }

string ArrayType::toString() const { return elemType_->toString() + "[]"; }

std::string ArrayType::mangle() const {
    std::string result = "A";
    result += elemType_->mangle();
    return result;
}

Type *ArrayType::clone(bool deep /* = false */) const {
    Type *newElemType = deep ? elemType_->clone(true) : elemType_;
    // 所有信息已知，直接使用 fromData 构建
    return fromData(newElemType, refCount_, refs_);
}

bool ArrayType::equals(Type *other) const {
    if (this == other) {
        return true;
    }
    if (!other || other->code() != TypeCode::Array) {
        return false;
    }
    const ArrayType &otherArr = static_cast<const ArrayType &>(*other);
    return elemType_->equals(otherArr.elemType_);
}

CastSafety ArrayType::castSafetyTo(Type *targetType) const {
    if (this == targetType) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool ArrayType::assignable(Type *type) const { return equals(type); }
