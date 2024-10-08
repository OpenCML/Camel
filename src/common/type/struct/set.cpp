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
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "set.h"
#include "array.h"
#include "vector.h"

using namespace std;

SetType::SetType(const type_ptr_t &valueType) : StructType(TypeCode::SET), valueType_(valueType) {}

type_ptr_t SetType::valueType() const { return valueType_; }

string SetType::toString() const { return "Set<" + valueType_->toString() + ">"; }

bool SetType::operator==(const Type &other) const {
    if (other.code() != TypeCode::SET) {
        return false;
    }
    const SetType &otherMap = dynamic_cast<const SetType &>(other);

    return valueType_->equals(otherMap.valueType_);
}
bool SetType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::SET) {
        return true;
    }
    const SetType &otherMap = dynamic_cast<const SetType &>(other);

    return !valueType_->equals(otherMap.valueType_);
}

TypeConv SetType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::SET:
            return valueType_->convertibility(*(dynamic_cast<const SetType &>(other).valueType_));
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            if (otherArray.size() == 1) {
                return valueType_->convertibility(*otherArray.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::DICT:
            [[fallthrough]];
        case TypeCode::UNION:
            [[fallthrough]];
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            return valueType_->convertibility(*otherVector.elementType());
        }
        case TypeCode::TENSOR:
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
