// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "type.h"
#include "camel/core/mm.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/other.h"
#include "camel/utils/assert.h"

#include <span>

namespace mm = camel::core::mm;
using namespace camel::core::type;

TypeCode PyObjectType::typeCode() {
    static TypeCode code = registerOtherType("PyObject", TypeFlag::GC_Traced);
    return code;
}

PyObjectType *PyObjectType::create() {
    // One object for the `PyObject` code, from the type table (see intern.h).
    return static_cast<PyObjectType *>(TypeTable::instance().internOther(
        typeCode(), std::span<const uintptr_t>(), [&]() -> Type * {
            void *mem = mm::permSpace().alloc(sizeof(PyObjectType), alignof(PyObjectType));
            ASSERT(mem != nullptr, "Failed to allocate PyObjectType from permSpace");
            return new (mem) PyObjectType(typeCode());
        }));
}

std::string PyObjectType::toString() const { return typeName(); }

std::string PyObjectType::mangle() const { return "PyObject"; }

Type *PyObjectType::clone(bool deep) const {
    (void)deep;
    return const_cast<PyObjectType *>(this);
}

bool PyObjectType::equals(Type *type) const {
    return type != nullptr && type->isOtherType() && type->code() == code_;
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

OtherType *PyObjectType::cloneWithParams(std::span<Type *const>) const {
    // `PyObject` takes no arguments.
    return getPyObjectType();
}

PyObjectType *getPyObjectType() {
    static PyObjectType *instance = PyObjectType::create();
    return instance;
}
