/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Jul. 29, 2025
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TensorType implementation: interning, printing, and the refinement lattice.
 */

#include "type.h"

#include "camel/core/mm.h"
#include "camel/utils/assert.h"
#include "dtype.h"

#include <mutex>
#include <unordered_map>

namespace mm = camel::core::mm;

namespace camel::tensor {

using namespace camel::core::type;

namespace {

std::string dimText(int64_t extent) { return extent == kUnknownDim ? "?" : std::to_string(extent); }

std::string
internKey(const std::optional<TypeCode> &dtype, const std::optional<StaticShape> &shape) {
    std::string key = dtype ? std::string(dtypeName(*dtype)) : "?";
    if (shape) {
        key += "[";
        for (int64_t extent : *shape) {
            key += dimText(extent) + ",";
        }
        key += "]";
    }
    return key;
}

} // namespace

TensorType::TensorType(std::optional<TypeCode> dtype, std::optional<StaticShape> shape)
    : OtherType(typeCode()), dtype_(dtype), shape_(std::move(shape)) {}

TypeCode TensorType::typeCode() {
    static TypeCode code = registerOtherType("Tensor", TypeFlag::GC_Traced);
    return code;
}

TensorType *TensorType::get(std::optional<TypeCode> dtype, std::optional<StaticShape> shape) {
    if (dtype) {
        dtype = normalizeTensorDType(*dtype);
    }
    if (shape) {
        for (int64_t &extent : *shape) {
            if (extent < 0) {
                extent = kUnknownDim;
            }
        }
    }
    static std::mutex mutex;
    static std::unordered_map<std::string, TensorType *> interned;
    const std::string key = internKey(dtype, shape);
    std::lock_guard guard(mutex);
    if (auto it = interned.find(key); it != interned.end()) {
        return it->second;
    }
    void *mem = mm::permSpace().alloc(sizeof(TensorType), alignof(TensorType));
    ASSERT(mem != nullptr, "Failed to allocate TensorType from permSpace");
    auto *created = new (mem) TensorType(dtype, std::move(shape));
    interned.emplace(key, created);
    return created;
}

TensorType *TensorType::Default() {
    static TensorType *instance = get(std::nullopt, std::nullopt);
    return instance;
}

bool TensorType::isStaticShape() const {
    if (!shape_) {
        return false;
    }
    for (int64_t extent : *shape_) {
        if (extent == kUnknownDim) {
            return false;
        }
    }
    return true;
}

std::string TensorType::toString() const {
    if (!dtype_ && !shape_) {
        return "Tensor";
    }
    std::string result = "Tensor<" + (dtype_ ? std::string(dtypeName(*dtype_)) : std::string("?"));
    if (shape_) {
        result += ", [";
        for (size_t i = 0; i < shape_->size(); ++i) {
            result += (i ? ", " : "") + dimText((*shape_)[i]);
        }
        result += "]";
    }
    return result + ">";
}

std::string TensorType::mangle() const { return "T" + internKey(dtype_, shape_) + ";"; }

Type *TensorType::clone(bool) const { return const_cast<TensorType *>(this); }

bool TensorType::equals(Type *other) const {
    // Interning makes structural equality pointer equality.
    return this == other;
}

Type *TensorType::unify(Type *other) const {
    const TensorType *rhs = asTensorType(other);
    if (!rhs) {
        return nullptr;
    }
    std::optional<TypeCode> dtype = dtype_ == rhs->dtype_ ? dtype_ : std::nullopt;
    std::optional<StaticShape> shape;
    if (shape_ && rhs->shape_ && shape_->size() == rhs->shape_->size()) {
        shape = StaticShape(shape_->size());
        for (size_t i = 0; i < shape->size(); ++i) {
            (*shape)[i] = (*shape_)[i] == (*rhs->shape_)[i] ? (*shape_)[i] : kUnknownDim;
        }
    }
    return get(dtype, std::move(shape));
}

Type *TensorType::widened() const { return Default(); }

CastSafety TensorType::castSafetyFrom(Type *sourceType) const {
    if (auto r = Type::checkCastSafetyWithAny(code(), sourceType)) {
        return *r;
    }
    return assignableFrom(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool TensorType::assignableFrom(Type *sourceType) const {
    const TensorType *rhs = asTensorType(sourceType);
    if (!rhs) {
        return false;
    }
    if (dtype_ && rhs->dtype_ != dtype_) {
        return false;
    }
    if (!shape_) {
        return true;
    }
    if (!rhs->shape_ || rhs->shape_->size() != shape_->size()) {
        return false;
    }
    for (size_t i = 0; i < shape_->size(); ++i) {
        if ((*shape_)[i] != kUnknownDim && (*shape_)[i] != (*rhs->shape_)[i]) {
            return false;
        }
    }
    return true;
}

OtherType *TensorType::cloneWithParams(std::span<Type *const> params) const {
    // `Tensor<dtype>`: the first parameter names the element type.
    if (params.empty()) {
        return Default();
    }
    return get(params[0]->code(), std::nullopt);
}

const TensorType *asTensorType(const Type *type) {
    if (!type || type->code() != TensorType::typeCode()) {
        return nullptr;
    }
    return static_cast<const TensorType *>(type);
}

} // namespace camel::tensor
