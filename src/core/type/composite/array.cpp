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
 * Updated: Nov. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "array.h"
#include "error/diagnostics/diagnostics.h"
#include "utils/assert.h"

using namespace std;

ArrayType::ArrayType(const type_ptr_t &elementType)
    : CompositeType(TypeCode::Array), elemType_(elementType) {}

std::shared_ptr<ArrayType> ArrayType::create(const type_ptr_t &elemType) const {
    static std::shared_ptr<ArrayType> voidArrayType = nullptr;
    if (!elemType) {
        if (voidArrayType == nullptr) {
            voidArrayType = std::make_shared<ArrayType>(Type::Void());
        }
        return voidArrayType;
    }
    return std::make_shared<ArrayType>(elemType);
}

type_ptr_t ArrayType::elementType() const { return elemType_; }

type_ptr_t ArrayType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "ArrayType is already resolved");

    type_ptr_t newElemType = elemType_;
    for (const auto &type : typeList) {
        if (newElemType->code() == TypeCode::Void) {
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

bool ArrayType::resolved() const { return elemType_->code() != TypeCode::Void; }

string ArrayType::toString() const {
    return (elemType_->code() == TypeCode::Void ? "" : elemType_->toString()) + "[]";
}

std::string ArrayType::mangle() const {
    std::string result = "V";
    result += elemType_->mangle();
    return result;
}

type_ptr_t ArrayType::clone() const { return ArrayType::create(elemType_); }

bool ArrayType::equals(const type_ptr_t &other) const {
    if (this == other.get()) {
        return true;
    }
    if (other->code() != TypeCode::Array) {
        return false;
    }
    const ArrayType &otherArr = static_cast<const ArrayType &>(*other);
    return elemType_->code() == TypeCode::Void || otherArr.elemType_->code() == TypeCode::Void ||
           elemType_->equals(otherArr.elemType_);
}

CastSafety ArrayType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool ArrayType::assignable(const type_ptr_t &type) const { return this->equals(type); }
