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

#include "tensor.h"

#include "array.h"
#include "set.h"
#include "vector.h"

using namespace std;

TensorType::TensorType(const type_ptr_t &elementType, const vector<size_t> &shape)
    : StructType(TypeCode::TENSOR), elementType_(elementType), shape_(shape) {
    if (shape_.size() == 0) {
        throw invalid_argument("Tensor shape must at least have 1 dim");
    }
    // element type must be a primitive type
    if (!elementType->primary()) {
        throw invalid_argument("Tensor element type must be primitive");
    }
}

vector<size_t> TensorType::shape() const { return shape_; }

type_ptr_t TensorType::elementType() const { return elementType_; }

string TensorType::toString() const {
    string result = "Tensor<" + elementType_->toString() + ", [";
    for (const auto &dim : shape_) {
        result += to_string(dim) + ", ";
    }
    if (!shape_.empty()) {
        result.pop_back();
        result.pop_back();
    }
    result += "]>";
    return result;
}

bool TensorType::operator==(const Type &other) const {
    if (other.code() != TypeCode::TENSOR) {
        return false;
    }
    const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
    return shape_ == otherMatrix.shape_ && elementType_->equals(otherMatrix.elementType_);
}

bool TensorType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::TENSOR) {
        return true;
    }
    const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
    return shape_ != otherMatrix.shape_ || !elementType_->equals(otherMatrix.elementType_);
}

TypeConv TensorType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::TENSOR: {
            const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
            if (shape_ == otherMatrix.shape()) {
                return elementType_->convertibility(*otherMatrix.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            return elementType_->convertibility(*otherVector.elementType());
        }
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            return elementType_->convertibility(*otherArray.elementType());
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
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}
