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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/composite/array.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"

using namespace std;
using namespace camel::core::error;
using namespace camel::core::type;

ArrayType *ArrayTypeFactory::build() {
    if (!elemType_) {
        elemType_ = Type::Void();
    }
    return ArrayType::fromFactory(*this);
}

ArrayType::ArrayType(Type *elemType)
    : CompositeType(TypeCode::Array), elemType_(elemType), elemTypeCode_(elemType->code()) {}

ArrayType *ArrayType::create(Type *elemType) {
    if (!elemType) {
        elemType = Type::Void();
    }

    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "ArrayType",
        "Allocating ArrayType: {}[], size: {} bytes",
        elemType->toString(),
        sizeof(ArrayType)));

    void *mem = mm::permSpace().alloc(sizeof(ArrayType), alignof(ArrayType));
    ASSERT(mem != nullptr, "Failed to allocate ArrayType from permSpace");
    return new (mem) ArrayType(elemType);
}

ArrayType *ArrayType::fromFactory(ArrayTypeFactory &factory) {
    if (!factory.elemType_) {
        factory.elemType_ = Type::Void();
    }
    return fromData(factory.elemType_);
}

ArrayType *ArrayType::fromData(Type *elemType) {
    if (!elemType) {
        elemType = Type::Void();
    }

    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "ArrayType",
        "Allocating ArrayType: {}[], size: {} bytes",
        elemType->toString(),
        sizeof(ArrayType)));

    void *mem = mm::permSpace().alloc(sizeof(ArrayType), alignof(ArrayType));
    ASSERT(mem != nullptr, "Failed to allocate ArrayType from permSpace");
    return new (mem) ArrayType(elemType);
}

Type *ArrayType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "ArrayType is already resolved");

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

bool ArrayType::resolved() const { return elemType_->code() != TypeCode::Ref; }

string ArrayType::toString() const { return elemType_->toString() + "[]"; }

std::string ArrayType::mangle() const {
    std::string result = "A";
    result += elemType_->mangle();
    return result;
}

Type *ArrayType::clone(bool deep /* = false */) const {
    Type *newElemType = deep ? elemType_->clone(true) : elemType_;
    return fromData(newElemType);
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

CastSafety ArrayType::castSafetyFrom(Type *sourceType) const {
    if (auto r = Type::checkCastSafetyWithAny(code(), sourceType))
        return *r;
    if (this == sourceType)
        return CastSafety::Safe;
    return CastSafety::Forbidden;
}

bool ArrayType::assignableFrom(Type *sourceType) const { return equals(sourceType); }
