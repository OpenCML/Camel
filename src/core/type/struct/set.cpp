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
 * Updated: Oct. 07, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "set.h"
#include "array.h"
#include "vector.h"

using namespace std;

SetType::SetType(const type_ptr_t &valueType) : StructType(TypeCode::Set), valueType_(valueType) {}

type_ptr_t SetType::valueType() const { return valueType_; }

string SetType::toString() const { return "Set<" + valueType_->toString() + ">"; }

std::optional<type_ptr_t> SetType::typeAt(struct_idx_t idx) const {
    ASSERT(false, "SetType does not support indexing");
    return std::nullopt;
}

bool SetType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    if (other.code() != TypeCode::Set) {
        return false;
    }
    const SetType &otherMap = dynamic_cast<const SetType &>(other);

    return valueType_->equals(otherMap.valueType_);
}
bool SetType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::Set) {
        return true;
    }
    const SetType &otherMap = dynamic_cast<const SetType &>(other);

    return !valueType_->equals(otherMap.valueType_);
}

CastSafety SetType::castSafetyTo(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::Set:
            return valueType_->castSafetyTo(*(dynamic_cast<const SetType &>(other).valueType_));
        case TypeCode::List:
            return CastSafety::Safe;
        case TypeCode::Array: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            if (otherArray.size() == 1) {
                return valueType_->castSafetyTo(*otherArray.elementType());
            }
            return CastSafety::Forbidden;
        }
        case TypeCode::Map:
            [[fallthrough]];
        case TypeCode::Dict:
            [[fallthrough]];
        case TypeCode::Union:
            [[fallthrough]];
        case TypeCode::Vector: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            return valueType_->castSafetyTo(*otherVector.elementType());
        }
        case TypeCode::Tensor:
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
