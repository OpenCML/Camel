/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "array.h"

#include "set.h"

using namespace std;

ArrayType::ArrayType(const type_ptr_t &elementType, size_t size)
    : StructType(TypeCode::ARRAY), size_(size), elementType_(elementType) {}

size_t ArrayType::size() const { return size_; }

type_ptr_t ArrayType::elementType() const { return elementType_; }

string ArrayType::toString() const {
    return "Array<" + elementType_->toString() + ", " + to_string(size_) + ">";
}

bool ArrayType::operator==(const Type &other) const {
    if (other.code() != TypeCode::ARRAY) {
        return false;
    }
    const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
    return size_ == otherArray.size_ && elementType_->equals(otherArray.elementType_);
}

bool ArrayType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::ARRAY) {
        return true;
    }
    const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
    return size_ != otherArray.size_ || !elementType_->equals(otherArray.elementType_);
}

TypeConv ArrayType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            return elementType_->convertibility(*otherArray.elementType());
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET: {
            const SetType &otherSet = dynamic_cast<const SetType &>(other);
            return elementType_->convertibility(*otherSet.valueType());
        }
        case TypeCode::VECTOR: {
            // we cannot know the size of the array
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::TENSOR: {
            // we cannot know the size of the array
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::DICT:
            [[fallthrough]];
        case TypeCode::UNION:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primary types and special types are forbidden
    return TypeConv::FORBIDDEN;
}
