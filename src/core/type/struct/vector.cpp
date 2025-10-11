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
 * Updated: Oct. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "vector.h"

#include "array.h"
#include "set.h"
#include "tensor.h"

using namespace std;

VectorType::VectorType(const type_ptr_t &elementType)
    : StructType(TypeCode::Vector), elementType_(elementType) {}

type_ptr_t VectorType::elementType() const { return elementType_; }

string VectorType::toString() const { return elementType_->toString() + "[]"; }

std::string VectorType::mangle() const {
    std::string result = "V";
    result += elementType_->mangle();
    return result;
}

std::optional<type_ptr_t> VectorType::typeAt(struct_idx_t idx) const { return elementType_; }

bool VectorType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    if (other.code() != TypeCode::Vector) {
        return false;
    }
    const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
    return elementType_->equals(otherVector.elementType_);
}

bool VectorType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::Vector) {
        return true;
    }
    const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
    return !elementType_->equals(otherVector.elementType_);
}

CastSafety VectorType::castSafetyTo(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::Vector: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            return elementType_->castSafetyTo(*otherVector.elementType());
        }
        case TypeCode::List:
            return CastSafety::Safe;
        case TypeCode::Array: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            return elementType_->castSafetyTo(*otherArray.elementType());
        }
        case TypeCode::Tensor: {
            const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
            const auto &shape = otherMatrix.shape();
            return elementType_->castSafetyTo(*otherMatrix.elementType());
        }
        case TypeCode::Set: {
            const SetType &otherSet = dynamic_cast<const SetType &>(other);
            return elementType_->castSafetyTo(*otherSet.valueType());
        }
        case TypeCode::Map:
            [[fallthrough]];
        case TypeCode::Dict:
            [[fallthrough]];
        case TypeCode::Union:
            return CastSafety::Forbidden;

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
