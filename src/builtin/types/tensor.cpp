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
 * Updated: Dec. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"

using namespace std;

unordered_map<string, string> TensorType::staticMethods_;

TensorType::TensorType(const vector<size_t> &shape)
    : OtherType(typeCode()), shape_(shape), element_type_(Type::Float32()) {
    if (shape_.size() == 0) {
        throw invalid_argument("Tensor shape must at least have 1 dim");
    }
}

TensorType::TensorType(const type_ptr_t &elementType, const vector<size_t> &shape)
    : OtherType(typeCode()), shape_(shape), element_type_(elementType) {
    if (shape_.size() == 0) {
        throw invalid_argument("Tensor shape must at least have 1 dim");
    }
}

vector<size_t> TensorType::shape() const { return shape_; }

type_ptr_t TensorType::dType() const { return element_type_; }

void TensorType::registerStaticMethod(
    const std::string &methodName, const std::string &operatorUri) {
    staticMethods_[methodName] = operatorUri;
}

std::string TensorType::getStaticMethodUri(const std::string &methodName) {
    auto it = staticMethods_.find(methodName);
    if (it != staticMethods_.end()) {
        return it->second;
    }
    return "";
}

bool TensorType::hasStaticMethod(const std::string &methodName) {
    return staticMethods_.find(methodName) != staticMethods_.end();
}

type_ptr_t TensorType::Tensor(const std::vector<size_t> &shape) {
    return std::make_shared<TensorType>(shape);
}

type_ptr_t TensorType::Default() {
    return std::make_shared<TensorType>(Type::Float32(), std::vector<size_t>{0});
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
    result += "]";

    if (element_type_ && !element_type_->equals(Type::Float64())) {
        result += ", " + element_type_->toString();
    }

    result += ">";
    return result;
}

std::string TensorType::mangle() const {
    std::string result = "T";
    for (size_t dim : shape_) {
        result += std::to_string(dim) + ",";
    }
    if (!shape_.empty()) {
        result.pop_back();
    }
    result += ";";

    if (element_type_) {
        result += element_type_->mangle();
    } else {
        result += "D";
    }

    return result;
}

type_ptr_t TensorType::clone(bool deep) const {
    ASSERT(false, "clone() not implemented");
    return nullptr;
}

bool TensorType::equals(const type_ptr_t &other) const {
    if (this == other.get()) {
        return true;
    }
    if (other->code() != typeCode()) {
        return false;
    }
    const TensorType &otherMatrix = dynamic_cast<const TensorType &>(*other);
    return shape_ == otherMatrix.shape_ && (element_type_ == otherMatrix.element_type_ ||
                                            (element_type_ && otherMatrix.element_type_ &&
                                             element_type_->equals(otherMatrix.element_type_)));
}

CastSafety TensorType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool TensorType::assignable(const type_ptr_t &type) const {
    // 目标必须是 Tensor 类型
    if (type->code() != typeCode()) {
        return false;
    }
    // 暂时先不考虑元素类型和形状的兼容性问题
    return true;
}
