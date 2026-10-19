/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Bytes is nominal and cannot be confused with a UTF-8 String or an integer array.
#include "camel/core/type/bytes.h"
#include "camel/core/mm.h"
namespace camel::core::type {
BytesType::BytesType() : OtherType(typeCode()) {}
TypeCode BytesType::typeCode() {
    static auto code = registerOtherType("Bytes", TypeFlag::GC_Traced);
    return code;
}
BytesType *BytesType::get() {
    static auto *type =
        new (mm::permSpace().alloc(sizeof(BytesType), alignof(BytesType))) BytesType();
    return type;
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
