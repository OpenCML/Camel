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
 * Updated: Nov. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "array.h"

#include "error/diagnostics/diagnostics.h"

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

string ArrayType::toString() const {
    return (elemType_->code() == TypeCode::Void ? "" : elemType_->toString()) + "[]";
}

std::string ArrayType::mangle() const {
    std::string result = "V";
    result += elemType_->mangle();
    return result;
}

std::optional<type_ptr_t> ArrayType::typeAt(struct_idx_t idx) const { return elemType_; }

bool ArrayType::resolved() const { return elemType_->code() != TypeCode::Void; }

void ArrayType::resolve(const type_vec_t &typeList) {
    ASSERT(typeList.size() > 0, "Type list is empty");
    for (const auto &type : typeList) {
        if (elemType_->code() == TypeCode::Void) {
            elemType_ = type;
        } else if (!elemType_->equals(type)) {
            throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
                .commit("Array", type->toString(), elemType_->toString());
        }
    }
}

bool ArrayType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    if (other.code() != TypeCode::Array) {
        return false;
    }
    const ArrayType &otherArr = dynamic_cast<const ArrayType &>(other);
    return elemType_->code() == TypeCode::Void || otherArr.elemType_->code() == TypeCode::Void ||
           elemType_->equals(otherArr.elemType_);
}

bool ArrayType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::Array) {
        return true;
    }
    const ArrayType &otherArr = dynamic_cast<const ArrayType &>(other);
    return !elemType_->equals(otherArr.elemType_);
}

type_ptr_t ArrayType::clone() const { return std::make_shared<ArrayType>(elemType_); }

CastSafety ArrayType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    if (other.code() == code_) {
        return CastSafety::Safe;
    }
    if (other.composed()) {
        switch (other.code()) {
        case TypeCode::Array: {
            if (elemType_->code() == TypeCode::Void) {
                return CastSafety::Safe;
            }
            const ArrayType &otherVector = dynamic_cast<const ArrayType &>(other);
            return elemType_->castSafetyTo(*otherVector.elementType());
        }

        default:
            return CastSafety::Forbidden;
        }
    }
    if (other.code() == TypeCode::Any) {
        return CastSafety::Safe;
    }
    // primary types and special types are forbidden
    return CastSafety::Forbidden;
}
