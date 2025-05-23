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
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "vector.h"

#include "array.h"
#include "set.h"
#include "tensor.h"

using namespace std;

VectorType::VectorType(const type_ptr_t &elementType) : StructType(TypeCode::VECTOR), elementType_(elementType) {}

type_ptr_t VectorType::elementType() const { return elementType_; }

string VectorType::toString() const { return "Vector<" + elementType_->toString() + ">"; }

bool VectorType::operator==(const Type &other) const {
    if (other.code() != TypeCode::VECTOR) {
        return false;
    }
    const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
    return elementType_->equals(otherVector.elementType_);
}

bool VectorType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::VECTOR) {
        return true;
    }
    const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
    return !elementType_->equals(otherVector.elementType_);
}

TypeConv VectorType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            return elementType_->convertibility(*otherVector.elementType());
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            return elementType_->convertibility(*otherArray.elementType());
        }
        case TypeCode::TENSOR: {
            const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
            const auto &shape = otherMatrix.shape();
            return elementType_->convertibility(*otherMatrix.elementType());
        }
        case TypeCode::SET: {
            const SetType &otherSet = dynamic_cast<const SetType &>(other);
            return elementType_->convertibility(*otherSet.valueType());
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
