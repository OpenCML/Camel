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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/slot.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace camel::core::type {

namespace type_impl = ::camel::core::type;

/*
 TypeCode (32 bits) encoding layout:

 bit31      bit30      bit29      bit28      bit27 ............... bit0
 +----------+----------+----------+----------+------------------------+
 |OtherType |Composite |GC_Traced |Auxiliary |      ID (28 bits)      |
 +----------+----------+----------+----------+------------------------+
 ^          ^          ^          ^          ^
 |          |          |          |          └─ Class-unique ID (0x0000000 ~ 0x0FFFFFFF)
 |          |          |          └─ bit28: auxiliary type (no actual data storage)
 |          |          └─ bit29: GC-traced type flag
 |          └─ bit30: composite type flag (Struct / Array / Tuple / ...)
 └─ bit31: third-party type flag (non-built-in type)

 Notes:
 - High-bit flags can be combined, e.g. Composite | GC_Traced
 - ID distinguishes concrete type numbers; only the low 28 bits are used
*/

enum class TypeFlag : uint32_t {
    Primitive = 0,        // Built-in primitive type: neither composite nor GC-traced.
    OtherType = 1u << 31, // Third-party type; actual IDs are registered at runtime.
    Composite = 1u << 30, // Composite type made from multiple types.
    GC_Traced = 1u << 29, // GC-traced type whose data object holds GC-managed references.
    Auxiliary = 1u << 28, // Auxiliary type used for resolution but with no data object.
    // Reference = 1u << 27  // Reserved reference type.
};

constexpr TypeFlag operator|(TypeFlag a, TypeFlag b) {
    return static_cast<TypeFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr uint32_t makeTypeCode(TypeFlag flags, uint32_t id) {
    return static_cast<uint32_t>(flags) | (id & 0x0FFFFFFF); // Store the ID in the low 28 bits.
}

enum class TypeCode : uint32_t {
    Int32    = makeTypeCode(TypeFlag::Primitive, 0x000),
    Int64    = makeTypeCode(TypeFlag::Primitive, 0x001),
    Float32  = makeTypeCode(TypeFlag::Primitive, 0x002),
    Float64  = makeTypeCode(TypeFlag::Primitive, 0x003),
    Bool     = makeTypeCode(TypeFlag::Primitive, 0x004),
    Byte     = makeTypeCode(TypeFlag::Primitive, 0x005),
    Void     = makeTypeCode(TypeFlag::Primitive, 0x006),
    String   = makeTypeCode(TypeFlag::Primitive | TypeFlag::GC_Traced, 0x007),
    Array    = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x008),
    Tuple    = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x009),
    Struct   = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x00A),
    Function = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x00B),
    Ref      = makeTypeCode(TypeFlag::Primitive | TypeFlag::Auxiliary, 0x00C),
    Any      = makeTypeCode(TypeFlag::Primitive | TypeFlag::Auxiliary, 0x00D),
    Union    = makeTypeCode(TypeFlag::Composite | TypeFlag::Auxiliary, 0x00E),
    Other    = makeTypeCode(TypeFlag::OtherType, 0x000),
};

inline constexpr bool hasFlag(uint32_t code, TypeFlag flag) {
    return (code & static_cast<uint32_t>(flag)) != 0;
}

// Note: isPrimitive means
// !isComposite && !isOtherType && !isAuxiliary
template <typename T> inline constexpr bool isPrimitive(T code) {
    // Primitive types have no flags set, i.e. the upper four bits are all 0.
    return (static_cast<uint32_t>(code) & 0xF0000000) == 0;
}

template <typename T> inline constexpr bool isOtherType(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::OtherType);
}

template <typename T> inline constexpr bool isComposite(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::Composite);
}

template <typename T> inline constexpr bool isGCTraced(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::GC_Traced);
}

template <typename T> inline constexpr bool isAuxiliary(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::Auxiliary);
}

enum class CastSafety {
    Safe      = 1,
    Unsafe    = -1,
    Forbidden = 0,
};

std::string typeCodeToString(TypeCode code);

extern const signed char primitiveTypeConvMatrix[8][8];

class Type;
using type_vec_t = std::vector<Type *>;

class Type {
  protected:
    TypeCode code_;

    /** Any-cast rule: converting between Any and any other type is always Safe. */
    static std::optional<CastSafety> checkCastSafetyWithAny(TypeCode targetCode, Type *sourceType);

  public:
    Type() = delete;
    Type(TypeCode type);
    virtual ~Type() noexcept = default;

    const TypeCode &code() const;

    inline bool isPrimitive() const { return type_impl::isPrimitive(code_); }
    inline bool isOtherType() const { return type_impl::isOtherType(code_); }
    inline bool isComposite() const { return type_impl::isComposite(code_); }
    inline bool isGCTraced() const { return type_impl::isGCTraced(code_); }
    inline bool isAuxiliary() const { return type_impl::isAuxiliary(code_); }

    virtual std::string toString() const;
    virtual std::string mangle() const;
    virtual Type *clone(bool deep = false) const;

    virtual bool equals(Type *type) const;
    virtual CastSafety castSafetyFrom(Type *sourceType) const;
    virtual slot_t castSlotFrom(slot_t value, Type *sourceType) const;
    bool assignableFrom(Type *sourceType) const;

    bool operator==(Type *other) const = delete;
    bool operator!=(Type *other) const = delete;

    static Type *create(TypeCode code);
    static Type *Int32();
    static Type *Int64();
    static Type *Int();
    static Type *Float32();
    static Type *Float64();
    static Type *Float();
    static Type *Bool();
    static Type *Byte();
    static Type *Void();
    static Type *String();
    static Type *Ref();
    static Type *Any();
};

} // namespace camel::core::type
