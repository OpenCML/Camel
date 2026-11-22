// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Bytes is nominal and cannot be confused with a UTF-8 String or an integer array.

#include "camel/core/type/bytes.h"
#include "camel/core/mm.h"
#include "camel/core/type/intern.h"
namespace camel::core::type {
BytesType::BytesType() : OtherType(typeCode()) {}
TypeCode BytesType::typeCode() {
    static auto code = registerOtherType("Bytes", TypeFlag::GC_Traced);
    return code;
}
BytesType *BytesType::get() {
    // One object for the one `Bytes` code, from the type table: a graph that records the type
    // names the table's entry, and `equals` is that one object (see intern.h).
    return static_cast<BytesType *>(TypeTable::instance().internOther(
        typeCode(), std::span<const uintptr_t>(), [&]() -> Type * {
            return new (mm::permSpace().alloc(sizeof(BytesType), alignof(BytesType))) BytesType();
        }));
}
std::string BytesType::toString() const { return "Bytes"; }
std::string BytesType::mangle() const { return "Bytes"; }
Type *BytesType::clone(bool) const { return get(); }
bool BytesType::equals(Type *other) const { return isBytesType(other); }
bool BytesType::assignableFrom(Type *other) const { return isBytesType(other); }
CastSafety BytesType::castSafetyFrom(Type *other) const {
    return isBytesType(other) ? CastSafety::Safe : CastSafety::Forbidden;
}
OtherType *BytesType::cloneWithParams(std::span<Type *const> params) const {
    return params.empty() ? get() : nullptr;
}
bool isBytesType(const Type *type) { return type && type->code() == BytesType::typeCode(); }
} // namespace camel::core::type
