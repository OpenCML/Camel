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
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"
#include "camel/core/mm.h"
#include "camel/utils/assert.h"

#include <span>

using namespace std;

unordered_map<string, string> TensorType::staticMethods_;

TensorType::TensorType(const vector<size_t> &shape)
    : OtherType(typeCode()), shape_(shape), element_type_(Type::Float32()) {
    if (shape_.size() == 0) {
        throw invalid_argument("Tensor shape must at least have 1 dim");
    }
}

TensorType::TensorType(Type *elementType, const std::vector<size_t> &shape)
    : OtherType(typeCode()), shape_(shape), element_type_(elementType) {
    if (shape_.size() == 0) {
        throw invalid_argument("Tensor shape must at least have 1 dim");
    }
}

TensorType::TensorType(TypeCode code, size_t paramCount, Type **params)
    : OtherType(code, paramCount, params), shape_(), element_type_(nullptr) {}

vector<size_t> TensorType::shape() const { return shape_; }

Type *TensorType::dType() const { return element_type_; }

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

TensorType *TensorType::create(Type *elementType, const std::vector<size_t> &shape) {
    void *mem = mm::permSpace().alloc(sizeof(TensorType), alignof(TensorType));
    ASSERT(mem != nullptr, "Failed to allocate TensorType from permSpace");
    return new (mem) TensorType(elementType, shape);
}

TensorType *TensorType::create(const std::vector<size_t> &shape) {
    return TensorType::create(Type::Float32(), shape);
}

Type *TensorType::Tensor(const std::vector<size_t> &shape) { return TensorType::create(shape); }

Type *TensorType::Default() { return TensorType::create(Type::Float32(), std::vector<size_t>{0}); }

string TensorType::toString() const {
    if (paramCount_ > 0 && params_) {
        string result = "Tensor<";
        for (size_t i = 0; i < paramCount_; ++i) {
            if (i > 0)
                result += ", ";
            result += params_[i] ? params_[i]->toString() : "?";
        }
        result += ">";
        return result;
    }
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
    if (paramCount_ > 0 && params_) {
        std::string result = "T";
        for (size_t i = 0; i < paramCount_; ++i) {
            if (i > 0)
                result += "_";
            result += params_[i] ? params_[i]->mangle() : "X";
        }
        return result;
    }
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

Type *TensorType::clone(bool deep) const {
    ASSERT(false, "clone() not implemented");
    return nullptr;
}

bool TensorType::equals(Type *other) const {
    if (this == other)
        return true;
    if (!other || !other->isOtherType() || other->code() != typeCode())
        return false;
    const auto *o = static_cast<const OtherType *>(other);
    if (paramCount_ > 0 && params_) {
        if (o->paramCount() != paramCount_)
            return false;
        std::span<Type *const> op = o->paramsSpan();
        for (size_t i = 0; i < paramCount_; ++i) {
            if ((params_[i] != nullptr) != (op[i] != nullptr))
                return false;
            if (params_[i] && !params_[i]->equals(op[i]))
                return false;
        }
        return true;
    }
    const TensorType &otherMatrix = dynamic_cast<const TensorType &>(*other);
    return shape_ == otherMatrix.shape_ && (element_type_ == otherMatrix.element_type_ ||
                                            (element_type_ && otherMatrix.element_type_ &&
                                             element_type_->equals(otherMatrix.element_type_)));
}

CastSafety TensorType::castSafetyTo(Type *targetType) const {
    if (this == targetType) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool TensorType::assignable(Type *type) const {
    // 目标必须是 Tensor 类型
    if (!type || type->code() != typeCode()) {
        return false;
    }
    // 暂时先不考虑元素类型和形状的兼容性问题
    return true;
}

OtherType *TensorType::cloneWithParams(std::span<Type *const> params) const {
    Type **p  = OtherType::copyParams(params);
    void *mem = mm::permSpace().alloc(sizeof(TensorType), alignof(TensorType));
    ASSERT(mem != nullptr, "Failed to allocate TensorType from permSpace");
    return new (mem) TensorType(typeCode(), params.size(), p);
}
