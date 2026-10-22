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
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/base.h"
#include "camel/core/mm.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/composite/composite.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/other.h"
#include "camel/core/type/storage.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"

using namespace std;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace camel::core::type {

const signed char primitiveTypeConvMatrix[6][6] = {
    // Int, Long, Float, Double, Bool, Void
    {01, 01, 01, 01, 01, 01}, // Int
    {-1, 01, -1, 01, 01, 01}, // Long
    {-1, -1, 01, 01, 01, 01}, // Float
    {-1, -1, -1, 01, 01, 01}, // Double
    {01, 01, 01, 01, 01, 01}, // Bool
    {00, 00, 00, 00, 00, 01}, // Void
};

namespace {

// Internal name -> the name a program writes. The only copy of this table.
constexpr std::pair<std::string_view, std::string_view> kSourceTypeNames[] = {
    {"i64", "int"},
    {"f64", "float"},
};

} // namespace

std::string sourceTypeSpelling(std::string_view text) {
    const auto isWord = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (isWord(text[i]) && (i == 0 || !isWord(text[i - 1]))) {
            size_t end = i;
            while (end < text.size() && isWord(text[end])) {
                ++end;
            }
            std::string_view word = text.substr(i, end - i);
            for (const auto &[internal, source] : kSourceTypeNames) {
                if (word == internal) {
                    word = source;
                    break;
                }
            }
            out += word;
            i = end;
            continue;
        }
        out += text[i++];
    }
    return out;
}

string typeCodeToString(TypeCode code) {
    switch (code) {
    case TypeCode::Int32:
        return "i32";
    case TypeCode::Int64:
        return std::string(kSourceTypeNames[0].second);
    case TypeCode::Float32:
        return "f32";
    case TypeCode::Float64:
        return std::string(kSourceTypeNames[1].second);
    case TypeCode::Bool:
        return "bool";
    case TypeCode::Void:
        return "void";
    case TypeCode::String:
        return "string";
    case TypeCode::Complex64:
        return "c64";
    case TypeCode::Complex128:
        return "c128";
    case TypeCode::Array:
        return "array";
    case TypeCode::Tuple:
        return "tuple";
    case TypeCode::Struct:
        return "struct";
    case TypeCode::Function:
        return "function";
    case TypeCode::Ref:
        return "ref";
    case TypeCode::TypeVar:
        return "typevar";
    case TypeCode::TypeFunc:
        return "typefunc";
    case TypeCode::EnumHole:
        return "_";
    default:
        return isOtherType(code) ? OtherTypeRegistry::getTypeName(code) : "unknown";
    }
}

Type::Type(TypeCode type) : code_(type) {}

const TypeCode &Type::code() const { return code_; }

std::string Type::toString() const { return typeCodeToString(code_); }

std::string Type::mangle() const {
    ASSERT(!camel::core::type::isComposite(code_), "Composite type cannot call Type::mangle");
    ASSERT(!camel::core::type::isOtherType(code_), "Other type cannot call Type::mangle");

    switch (code_) {
    case TypeCode::Int32:
        return "i32";
    case TypeCode::Int64:
        return "i";
    case TypeCode::Float32:
        return "f32";
    case TypeCode::Float64:
        return "f";
    case TypeCode::Bool:
        return "b";
    case TypeCode::Void:
        return "v";
    case TypeCode::String:
        return "s";
    case TypeCode::Complex64:
        return "c64";
    case TypeCode::Complex128:
        return "c128";
    case TypeCode::Ref:
        return "r";
    case TypeCode::EnumHole:
        return "H";
    default:
        ASSERT(false, "Unknown TypeCode");
        return "";
    }

    return ""; // unknown
}

Type *Type::clone(bool deep) const {
    ASSERT(!camel::core::type::isComposite(code_), "Composite type cannot call Type::clone");
    ASSERT(!camel::core::type::isOtherType(code_), "Other type cannot call Type::clone");
    return Type::create(code_);
}

bool Type::equals(Type *type) const {
    ASSERT(!camel::core::type::isComposite(code_), "Composite type cannot call Type::equals");
    ASSERT(!camel::core::type::isOtherType(code_), "Other type cannot call Type::equals");
    if (!type)
        return false;
    return code_ == type->code_;
}

Type *Type::unify(Type *other) const { return equals(other) ? const_cast<Type *>(this) : nullptr; }

Type *Type::widened() const { return const_cast<Type *>(this); }

CastSafety Type::castSafetyFrom(Type *sourceType) const {
    ASSERT(
        !camel::core::type::isComposite(code_),
        "Composite type cannot call Type::castSafetyFrom");
    ASSERT(!camel::core::type::isOtherType(code_), "Other type cannot call Type::castSafetyFrom");

    if (!sourceType)
        return CastSafety::Forbidden;

    // Converting a value to its own type is legal and does nothing (design principles 5.1).
    // The composite types decide this themselves; this covers the ones that live in a slot
    // (the primitives, a text, the complex numbers) and any other type without fields.
    if (equals(sourceType))
        return CastSafety::Safe;

    // Exclude auxiliary types first.
    if (camel::core::type::isAuxiliary(code_) ||
        camel::core::type::isAuxiliary(sourceType->code())) {
        return CastSafety::Forbidden;
    }

    // After excluding auxiliary types, only eight primitive types remain.
    // The matrix is [from][to], i.e. [sourceIndex][targetIndex].
    if (camel::core::type::isPrimitive(code_) &&
        camel::core::type::isPrimitive(sourceType->code())) {
        const int sourceIndex = static_cast<int>(sourceType->code()) & 0b1111;
        const int targetIndex = static_cast<int>(code_) & 0b1111;
        return static_cast<CastSafety>(primitiveTypeConvMatrix[sourceIndex][targetIndex]);
    }

    // All remaining conversions are disallowed by default.
    return CastSafety::Forbidden;
}

slot_t Type::castSlotFrom(slot_t value, Type *sourceType) const {
    ASSERT(sourceType != nullptr, "castSlotFrom source type is null");
    ASSERT(
        castSafetyFrom(sourceType) == CastSafety::Safe,
        "castSlotFrom only allowed when castSafetyFrom returns Safe");
    ASSERT(
        !camel::core::type::isComposite(code_),
        "castSlotFrom not implemented for composite types");
    ASSERT(!camel::core::type::isOtherType(code_), "castSlotFrom not implemented for other types");

    const TypeCode from = sourceType->code();
    const TypeCode to   = code_;

    using camel::core::rtdata::Bool;
    using camel::core::rtdata::Float32;
    using camel::core::rtdata::Float64;
    using camel::core::rtdata::Int32;
    using camel::core::rtdata::Int64;

#define DO_CONV(FromT, ToT, expr) return toSlot(static_cast<ToT>(expr))

    // Same type
    if (from == to)
        return value;

    switch (from) {
    case TypeCode::Int32: {
        Int32 v = fromSlot<Int32>(static_cast<slot_t>(value));
        switch (to) {
        case TypeCode::Int64:
            DO_CONV(Int32, Int64, v);
        case TypeCode::Float32:
            DO_CONV(Int32, Float32, v);
        case TypeCode::Float64:
            DO_CONV(Int32, Float64, v);
        case TypeCode::Bool:
            DO_CONV(Int32, Bool, v != 0);
        case TypeCode::String: {
            ::String *s = ::String::from(std::to_string(v), mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotFrom Int32->unsupported target");
        }
    }
    case TypeCode::Int64: {
        Int64 v = fromSlot<Int64>(static_cast<slot_t>(value));
        switch (to) {
        case TypeCode::Int32:
            return toSlot(checkedNarrow<Int32>(v, "i32"));
        case TypeCode::Float32:
            DO_CONV(Int64, Float32, v);
        case TypeCode::Float64:
            DO_CONV(Int64, Float64, v);
        case TypeCode::Bool:
            DO_CONV(Int64, Bool, v != 0);
        case TypeCode::String: {
            ::String *s = ::String::from(std::to_string(v), mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotFrom Int64->unsupported target");
        }
    }
    case TypeCode::Float32: {
        Float32 v = fromSlot<Float32>(static_cast<slot_t>(value));
        switch (to) {
        case TypeCode::Int32:
            return toSlot(checkedTruncate<Int32>(v, "i32"));
        case TypeCode::Int64:
            return toSlot(checkedTruncate<Int64>(v, "int"));
        case TypeCode::Float64:
            DO_CONV(Float32, Float64, v);
        case TypeCode::Bool:
            DO_CONV(Float32, Bool, v != 0);
        case TypeCode::String: {
            ::String *s = ::String::from(camel::utils::floatText(v), mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotFrom Float32->unsupported target");
        }
    }
    case TypeCode::Float64: {
        Float64 v = fromSlot<Float64>(static_cast<slot_t>(value));
        switch (to) {
        case TypeCode::Int32:
            return toSlot(checkedTruncate<Int32>(v, "i32"));
        case TypeCode::Int64:
            return toSlot(checkedTruncate<Int64>(v, "int"));
        case TypeCode::Float32:
            DO_CONV(Float64, Float32, v);
        case TypeCode::Bool:
            DO_CONV(Float64, Bool, v != 0);
        case TypeCode::String: {
            ::String *s = ::String::from(camel::utils::floatText(v), mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotFrom Float64->unsupported target");
        }
    }
    case TypeCode::Bool: {
        Bool v = fromSlot<Bool>(static_cast<slot_t>(value));
        switch (to) {
        case TypeCode::Int32:
            DO_CONV(Bool, Int32, v ? 1 : 0);
        case TypeCode::Int64:
            DO_CONV(Bool, Int64, v ? 1 : 0);
        case TypeCode::Float32:
            DO_CONV(Bool, Float32, v ? 1.0f : 0.0f);
        case TypeCode::Float64:
            DO_CONV(Bool, Float64, v ? 1.0 : 0.0);
        case TypeCode::String: {
            ::String *s = ::String::from(v ? "true" : "false", mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotFrom Bool->unsupported target");
        }
    }
    case TypeCode::Void:
        if (to == TypeCode::String) {
            ::String *s = ::String::from("null", mm::autoSpace());
            return toSlot(s);
        }
        ASSERT(false, "castSlotFrom Void->unsupported target");
    case TypeCode::String: {
        ::String *s = reinterpret_cast<::String *>(static_cast<uintptr_t>(value));
        switch (to) {
        // The whole string must be one number; otherwise the fault of the conversion
        // functions (design principles 5.1).
        case TypeCode::Int32:
            return toSlot(integerFromText<Int32>(s->view(), "i32"));
        case TypeCode::Int64:
            return toSlot(integerFromText<Int64>(s->view(), "int"));
        case TypeCode::Float32:
            return toSlot(floatFromText<Float32>(s->view(), "f32"));
        case TypeCode::Float64:
            return toSlot(floatFromText<Float64>(s->view(), "float"));
        case TypeCode::String:
            return value;
        default:
            ASSERT(false, "castSlotFrom String->unsupported target");
        }
    }
    default:
        ASSERT(false, "castSlotFrom unsupported source type");
    }

#undef DO_CONV
    return 0; // unreachable
}

bool Type::assignableFrom(Type *from) const {
    const Type *to = this;

    if (!from || to == from)
        return true;

    // Built-in types: composite types containing Ref must be resolved before assignment.
    ASSERT(
        to->code_ != TypeCode::Ref && from->code_ != TypeCode::Ref,
        "Ref type cannot be assigned");

    // A type variable stands for a type that is not known yet: it only matches itself.
    if (to->code_ == TypeCode::TypeVar || from->code_ == TypeCode::TypeVar)
        return to->equals(from);

    // Void can accept assignment from any type, but cannot be assigned to other types.
    if (to->code_ == TypeCode::Void)
        return true;
    if (from->code_ == TypeCode::Void)
        return false;

    // Third-party types rely on their own overridden assignableFrom. This must be
    // checked before the composite flag: an Other type may also be flagged
    // Composite (e.g. Tensor) but is never a CompositeType object.
    if (camel::core::type::isOtherType(to->code_)) {
        const auto &self = static_cast<const OtherType &>(*this);
        return self.assignableFrom(from);
    }

    // Built-in composite types must override assignableFrom.
    if (camel::core::type::isComposite(to->code_)) {
        const auto &self = static_cast<const CompositeType &>(*this);
        return self.assignableFrom(from);
    }

    // Remaining cases are assignments between primitive types, which must match exactly.
    return to->code_ == from->code_;
}

Type *Type::create(TypeCode code) {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "Type",
        "Allocating Type: {}, size: {} bytes",
        typeCodeToString(code),
        sizeof(Type)));
    void *mem = allocTypeStorage<Type>(TypeStorageKind::Primitive);
    return new (mem) Type(code);
}

namespace {

/// The one object of a primitive type, registered with the type table so that it has a TypeId.
Type *primitiveSingleton(TypeCode code) {
    Type *type = Type::create(code);
    TypeTable::instance().adopt(type);
    return type;
}

} // namespace

Type *Type::Int32() {
    static Type *type = primitiveSingleton(TypeCode::Int32);
    return type;
}

Type *Type::Int64() {
    static Type *type = primitiveSingleton(TypeCode::Int64);
    return type;
}

Type *Type::Int() { return Type::Int64(); }

Type *Type::Float32() {
    static Type *type = primitiveSingleton(TypeCode::Float32);
    return type;
}

Type *Type::Float64() {
    static Type *type = primitiveSingleton(TypeCode::Float64);
    return type;
}

Type *Type::Float() { return Type::Float64(); }

Type *Type::Bool() {
    static Type *type = primitiveSingleton(TypeCode::Bool);
    return type;
}

Type *Type::Void() {
    static Type *type = primitiveSingleton(TypeCode::Void);
    return type;
}

Type *Type::String() {
    static Type *type = primitiveSingleton(TypeCode::String);
    return type;
}

Type *Type::Complex64() {
    static Type *type = primitiveSingleton(TypeCode::Complex64);
    return type;
}

Type *Type::Complex128() {
    static Type *type = primitiveSingleton(TypeCode::Complex128);
    return type;
}

Type *Type::Ref() {
    static Type *type = primitiveSingleton(TypeCode::Ref);
    return type;
}

} // namespace camel::core::type
