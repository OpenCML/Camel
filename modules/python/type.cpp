// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "type.h"
#include "camel/core/mm.h"
#include "camel/core/type/other.h"
#include "camel/utils/assert.h"

#include <span>

namespace mm = camel::core::mm;
using namespace camel::core::type;

TypeCode PyObjectType::typeCode() {
    static TypeCode code = registerOtherType("PyObject", TypeFlag::GC_Traced);
    return code;
}

PyObjectType::PyObjectType(TypeCode code, size_t paramCount, Type **params)
    : OtherType(code, paramCount, params) {}

PyObjectType *PyObjectType::create() {
    void *mem = mm::permSpace().alloc(sizeof(PyObjectType), alignof(PyObjectType));
    ASSERT(mem != nullptr, "Failed to allocate PyObjectType from permSpace");
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

CastSafety PyObjectType::castSafetyFrom(Type *sourceType) const {
    return sourceType && sourceType->code() == code_ ? CastSafety::Safe : CastSafety::Forbidden;
}

slot_t PyObjectType::castSlotFrom(slot_t value, Type *sourceType) const {
    (void)sourceType;
    return value;
}

bool PyObjectType::assignableFrom(Type *sourceType) const {
    return sourceType && sourceType->code() == code_;
}

OtherType *PyObjectType::cloneWithParams(std::span<Type *const> params) const {
    Type **p  = OtherType::copyParams(params);
    void *mem = mm::permSpace().alloc(sizeof(PyObjectType), alignof(PyObjectType));
    ASSERT(mem != nullptr, "Failed to allocate PyObjectType from permSpace");
    return new (mem) PyObjectType(code_, params.size(), p);
}

PyObjectType *getPyObjectType() {
    static PyObjectType *instance = PyObjectType::create();
    return instance;
}
