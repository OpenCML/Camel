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
 * Updated: Sep. 26, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "array.h"
#include "vector.h"

#include "set.h"

using namespace std;

ArrayType::ArrayType(const type_ptr_t &elementType, size_t size)
    : StructType(TypeCode::Array), size_(size), elementType_(elementType) {}

size_t ArrayType::size() const { return size_; }

type_ptr_t ArrayType::elementType() const { return elementType_; }

string ArrayType::toString() const {
    return "Array<" + elementType_->toString() + ", " + to_string(size_) + ">";
}

bool ArrayType::operator==(const Type &other) const {
    if (other.code() != TypeCode::Array) {
        return false;
    }
    const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
    return size_ == otherArray.size_ && elementType_->equals(otherArray.elementType_);
}

bool ArrayType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::Array) {
        return true;
    }
    const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
    return size_ != otherArray.size_ || !elementType_->equals(otherArray.elementType_);
}

CastSafety ArrayType::castSafetyTo(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::Array: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            return elementType_->castSafetyTo(*otherArray.elementType());
        }
        case TypeCode::List:
            return CastSafety::Safe;
        case TypeCode::Set: {
            const SetType &otherSet = dynamic_cast<const SetType &>(other);
            return elementType_->castSafetyTo(*otherSet.valueType());
        }
        case TypeCode::Vector: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            return elementType_->castSafetyTo(*otherVector.elementType());
        }
        case TypeCode::Tensor: {
            // we cannot know the size of the array
            return CastSafety::Forbidden;
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
