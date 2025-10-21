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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"

#include "set.h"
#include "vector.h"

using namespace std;

unordered_map<string, string> TensorType::staticMethods_;

TensorType::TensorType(const vector<size_t> &shape)
    : StructType(TypeCode::Tensor), shape_(shape) {
    if (shape_.size() == 0) {
        throw invalid_argument("Tensor shape must at least have 1 dim");
    }
}

vector<size_t> TensorType::shape() const { return shape_; }

type_ptr_t TensorType::elementType() const { 
    // TODO: element type
    return Type::Any();
}

string TensorType::toString() const {
    string result = "Tensor<[";
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

std::string TensorType::mangle() const {
    std::string result = "T";
    result += elementType_->mangle();
    result += std::to_string(shape_.size());
    result += "_";
    for (const auto &dim : shape_) {
        result += std::to_string(dim) + "_";
    }
    return result;
}

bool TensorType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    if (other.code() != typeCode()) {
        return false;
    }
    const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
    return shape_ == otherMatrix.shape_;
}

bool TensorType::operator!=(const Type &other) const {
    if (other.code() != typeCode()) {
        return true;
    }
    const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
    return shape_ != otherMatrix.shape_;
}

CastSafety TensorType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    if (other.code() == typeCode()) {
        return CastSafety::Safe;
    }
    if (other.composed()) {
        switch (other.code()) {
        case TypeCode::Tensor: {
            const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
            if (shape_ == otherMatrix.shape()) {
                return CastSafety::Safe;
            }
            return CastSafety::Forbidden;
        }
        case TypeCode::List:
            return CastSafety::Safe;
        case TypeCode::Vector: {
            return CastSafety::Safe;
        }
        case TypeCode::Array: {
            return CastSafety::Safe;
        }
        case TypeCode::Set: {
            return CastSafety::Safe;
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
    return CastSafety::Forbidden;
}
void TensorType::registerStaticMethod(const std::string& methodName, const std::string& operatorUri) {
    staticMethods_[methodName] = operatorUri;
}


std::string TensorType::getStaticMethodUri(const std::string& methodName) {
    auto it = staticMethods_.find(methodName);
    if (it != staticMethods_.end()) {
        return it->second;
    }
    return "";
}

bool TensorType::hasStaticMethod(const std::string& methodName) {
    return staticMethods_.find(methodName) != staticMethods_.end();
}

type_ptr_t TensorType::Tensor(const std::vector<size_t>& shape) {
    return std::make_shared<TensorType>(shape);
}