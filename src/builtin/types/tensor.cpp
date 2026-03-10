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
 * Updated: Mar. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"
#include "camel/core/mm.h"
#include "camel/utils/assert.h"

#include <span>

namespace mm = camel::core::mm;
using namespace std;
using namespace camel::core::type;

unordered_map<string, string> TensorType::staticMethods_;

TypeCode TensorType::typeCode() {
    static TypeCode code = registerOtherType("Tensor", TypeFlag::Composite | TypeFlag::GC_Traced);
    return code;
}

TensorType::TensorType(const vector<size_t> &shape)
    : OtherType(typeCode()), shape_(shape), element_type_(Type::Float32()) {}

TensorType::TensorType(Type *elementType, const vector<size_t> &shape)
    : OtherType(typeCode()), shape_(shape),
      element_type_(elementType ? elementType : Type::Float32()) {}

TensorType::TensorType(TypeCode code, size_t paramCount, Type **params)
    : OtherType(code, paramCount, params), shape_(), element_type_(Type::Float32()) {}

vector<size_t> TensorType::shape() const { return shape_; }

Type *TensorType::dType() const { return element_type_ ? element_type_ : Type::Float32(); }

void TensorType::registerStaticMethod(
    const std::string &methodName, const std::string &operatorUri) {
    staticMethods_[methodName] = operatorUri;
}

std::string TensorType::getStaticMethodUri(const std::string &methodName) {
    auto it = staticMethods_.find(methodName);
    return it == staticMethods_.end() ? "" : it->second;
}

bool TensorType::hasStaticMethod(const std::string &methodName) {
    return staticMethods_.find(methodName) != staticMethods_.end();
}

TensorType *TensorType::create(Type *elementType, const vector<size_t> &shape) {
    void *mem = mm::permSpace().alloc(sizeof(TensorType), alignof(TensorType));
    ASSERT(mem != nullptr, "Failed to allocate TensorType from permSpace");
    return new (mem) TensorType(elementType, shape);
}

TensorType *TensorType::create(const vector<size_t> &shape) {
    return TensorType::create(Type::Float32(), shape);
}

TensorType *TensorType::Dynamic(Type *elementType) {
    static TensorType *floatDynamic = TensorType::create(Type::Float32(), {});
    if (!elementType || elementType->equals(Type::Float32())) {
        return floatDynamic;
    }
    return TensorType::create(elementType, {});
}

Type *TensorType::Tensor(const vector<size_t> &shape) { return TensorType::create(shape); }

Type *TensorType::Default() { return TensorType::Dynamic(Type::Float32()); }

string TensorType::toString() const {
    const Type *dtype = dType();
    if (shape_.empty()) {
        if (!dtype || dtype->equals(Type::Float32())) {
            return "Tensor";
        }
        return "Tensor<" + dtype->toString() + ">";
    }

    string result = "Tensor<[";
    for (size_t i = 0; i < shape_.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += to_string(shape_[i]);
    }
    result += "]";
    if (dtype && !dtype->equals(Type::Float32())) {
        result += ", " + dtype->toString();
    }
    result += ">";
    return result;
}

std::string TensorType::mangle() const {
    std::string result = "T";
    if (shape_.empty()) {
        result += "_";
    } else {
        for (size_t i = 0; i < shape_.size(); ++i) {
            if (i > 0) {
                result += ",";
            }
            result += std::to_string(shape_[i]);
        }
    }
    result += ";";
    result += dType()->mangle();
    return result;
}

Type *TensorType::clone(bool deep) const {
    Type *dtype = deep ? dType()->clone(true) : dType();
    return TensorType::create(dtype, shape_);
}

bool TensorType::equals(Type *other) const {
    if (this == other) {
        return true;
    }
    if (!other || !other->isOtherType() || other->code() != typeCode()) {
        return false;
    }
    const auto *rhs = dynamic_cast<const TensorType *>(other);
    if (!rhs) {
        return false;
    }
    return shape_ == rhs->shape() &&
           (dType() == rhs->dType() || (dType() && rhs->dType() && dType()->equals(rhs->dType())));
}

CastSafety TensorType::castSafetyFrom(Type *sourceType) const {
    if (auto r = Type::checkCastSafetyWithAny(code(), sourceType)) {
        return *r;
    }
    if (!sourceType || sourceType->code() != typeCode()) {
        return CastSafety::Forbidden;
    }
    return assignableFrom(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool TensorType::assignableFrom(Type *sourceType) const {
    if (!sourceType || sourceType->code() != typeCode()) {
        return false;
    }
    auto *rhs = dynamic_cast<TensorType *>(sourceType);
    if (!rhs) {
        return false;
    }
    bool dtypeCompat = dType()->equals(Type::Float32()) || dType()->equals(rhs->dType());
    bool shapeCompat = shape_.empty() || rhs->shape().empty() || shape_ == rhs->shape();
    return dtypeCompat && shapeCompat;
}

OtherType *TensorType::cloneWithParams(std::span<Type *const> params) const {
    if (params.empty()) {
        return TensorType::Dynamic();
    }
    if (params.size() == 1) {
        return TensorType::Dynamic(params[0]);
    }
    Type **p  = OtherType::copyParams(params);
    void *mem = mm::permSpace().alloc(sizeof(TensorType), alignof(TensorType));
    ASSERT(mem != nullptr, "Failed to allocate TensorType from permSpace");
    return new (mem) TensorType(typeCode(), params.size(), p);
}
