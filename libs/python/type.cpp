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
 * Created: Feb. 20, 2026
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "type.h"
#include "camel/core/mm.h"
#include "camel/utils/assert.h"

#include <span>

TypeCode PyObjectType::typeCode() {
    static TypeCode code = registerOtherType("PyObject", TypeFlag::GC_Traced);
    return code;
}

PyObjectType::PyObjectType(TypeCode code, size_t paramCount, Type **params)
    : OtherType(code, paramCount, params) {}

PyObjectType *PyObjectType::create() {
    void *mem = mm::autoSpace().alloc(sizeof(PyObjectType), alignof(PyObjectType));
    ASSERT(mem != nullptr, "Failed to allocate PyObjectType from autoSpace");
    return new (mem) PyObjectType(typeCode());
}

std::string PyObjectType::toString() const {
    std::string s = typeName();
    if (paramCount_ > 0 && params_) {
        s += "<";
        for (size_t i = 0; i < paramCount_; ++i) {
            if (i > 0)
                s += ", ";
            s += params_[i] ? params_[i]->toString() : "?";
        }
        s += ">";
    }
    return s;
}

std::string PyObjectType::mangle() const {
    std::string s = "PyObject";
    if (paramCount_ > 0 && params_) {
        for (size_t i = 0; i < paramCount_; ++i) {
            s += "_";
            s += params_[i] ? params_[i]->mangle() : "X";
        }
    }
    return s;
}

Type *PyObjectType::clone(bool deep) const {
    (void)deep;
    return const_cast<PyObjectType *>(this);
}

bool PyObjectType::equals(Type *type) const {
    if (!type || !type->isOtherType() || type->code() != code_)
        return false;
    const auto *o = static_cast<const OtherType *>(type);
    if (paramCount_ != o->paramCount())
        return false;
    std::span<Type *const> op = o->paramsSpan();
    if (!params_ || op.empty())
        return (params_ == nullptr) == op.empty();
    for (size_t i = 0; i < paramCount_; ++i) {
        if ((params_[i] != nullptr) != (op[i] != nullptr))
            return false;
        if (params_[i] && !params_[i]->equals(op[i]))
            return false;
    }
    return true;
}

CastSafety PyObjectType::castSafetyTo(Type *targetType) const {
    return targetType && targetType->code() == code_ ? CastSafety::Safe : CastSafety::Forbidden;
}

slot_t PyObjectType::castSlotTo(slot_t value, Type *targetType) const {
    (void)targetType;
    return value;
}

bool PyObjectType::assignable(Type *type) const {
    return type && type->code() == code_;
}

OtherType *PyObjectType::cloneWithParams(std::span<Type *const> params) const {
    Type **p = OtherType::copyParams(params);
    void *mem = mm::autoSpace().alloc(sizeof(PyObjectType), alignof(PyObjectType));
    ASSERT(mem != nullptr, "Failed to allocate PyObjectType from autoSpace");
    return new (mem) PyObjectType(code_, params.size(), p);
}

PyObjectType *getPyObjectType() {
    static PyObjectType *instance = PyObjectType::create();
    return instance;
}
