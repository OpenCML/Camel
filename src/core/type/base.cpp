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
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/base.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/composite/composite.h"
#include "camel/core/type/other.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"

#include <sstream>

using namespace std;

const signed char primitiveTypeConvMatrix[8][8] = {
    // Int, Long, Float, Double, Bool, Byte, Void, String
    {01, 01, 01, 01, 01, -1, 01}, // Int
    {-1, 01, -1, 01, 01, -1, 01}, // Long
    {-1, -1, 01, 01, 01, -1, 01}, // Float
    {-1, -1, -1, 01, 01, -1, 01}, // Double
    {01, 01, 01, 01, 01, 01, 01}, // Bool
    {01, 01, 01, 01, 01, 01, 01}, // Byte
    {00, 00, 00, 00, 00, 00, 01}, // Void
    {00, 00, 00, 00, 00, 00, 01}  // String
};

string typeCodeToString(TypeCode code) {
    switch (code) {
    case TypeCode::Int32:
        return "i32";
    case TypeCode::Int64:
        return "i64";
    case TypeCode::Float32:
        return "f32";
    case TypeCode::Float64:
        return "f64";
    case TypeCode::Bool:
        return "bool";
    case TypeCode::Byte:
        return "byte";
    case TypeCode::Void:
        return "void";
    case TypeCode::String:
        return "string";
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
    case TypeCode::Any:
        return "any";
    case TypeCode::Union:
        return "union";
    default:
        return isOtherType(code) ? OtherTypeRegistry::getTypeName(code) : "unknown";
    }
}

Type::Type(TypeCode type) : code_(type) {}

const TypeCode &Type::code() const { return code_; }

std::string Type::toString() const { return typeCodeToString(code_); }

std::string Type::mangle() const {
    ASSERT(!::isComposite(code_), "Composite type cannot call Type::mangle");
    ASSERT(!::isOtherType(code_), "Other type cannot call Type::mangle");

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
    case TypeCode::Byte:
        return "c";
    case TypeCode::Void:
        return "v";
    case TypeCode::String:
        return "s";
    case TypeCode::Ref:
        return "r";
    case TypeCode::Any:
        return "a";
    default:
        ASSERT(false, "Unknown TypeCode");
        return "";
    }

    return ""; // unknown
}

Type *Type::clone(bool deep) const {
    ASSERT(!::isComposite(code_), "Composite type cannot call Type::clone");
    ASSERT(!::isOtherType(code_), "Other type cannot call Type::clone");
    return Type::create(code_);
}

bool Type::equals(Type *type) const {
    ASSERT(!::isComposite(code_), "Composite type cannot call Type::equals");
    ASSERT(!::isOtherType(code_), "Other type cannot call Type::equals");
    if (!type)
        return false;
    return code_ == type->code_;
}

std::optional<CastSafety> Type::checkCastSafetyWithAny(TypeCode fromCode, Type *targetType) {
    if (!targetType)
        return CastSafety::Forbidden;
    // 通用规则：任何类型 <-> Any 均为 Safe
    if (fromCode == TypeCode::Any || targetType->code() == TypeCode::Any)
        return CastSafety::Safe;
    return std::nullopt;
}

CastSafety Type::castSafetyTo(Type *targetType) const {
    ASSERT(!::isComposite(code_), "Composite type cannot call Type::castSafetyTo");
    ASSERT(!::isOtherType(code_), "Other type cannot call Type::castSafetyTo");

    if (auto r = checkCastSafetyWithAny(code_, targetType))
        return *r;

    if (!targetType)
        return CastSafety::Forbidden;

    // 这里要先排除掉辅助类型（Any 已由 checkCastSafetyWithAny 处理）
    if (::isAuxiliary(code_) || ::isAuxiliary(targetType->code())) {
        return CastSafety::Forbidden;
    }

    // 排除掉辅助类型后，原始类型只剩8个
    if (::isPrimitive(code_) && ::isPrimitive(targetType->code())) {
        const int thisIndex  = static_cast<int>(code_) & 0b1111;
        const int otherIndex = static_cast<int>(targetType->code()) & 0b1111;
        return static_cast<CastSafety>(primitiveTypeConvMatrix[thisIndex][otherIndex]);
    }

    // 剩余所有类型默认不允许转换
    return CastSafety::Forbidden;
}

slot_t Type::castSlotTo(slot_t value, Type *targetType) const {
    ASSERT(targetType != nullptr, "castSlotTo target type is null");
    ASSERT(
        castSafetyTo(targetType) == CastSafety::Safe,
        "castSlotTo only allowed when castSafetyTo returns Safe");
    // Any 与任何类型互相转换时直接透传 slot 值（需在所有 ASSERT 之前，以支持 composite 等类型）
    if (code_ == TypeCode::Any || targetType->code() == TypeCode::Any)
        return value;

    ASSERT(!::isComposite(code_), "castSlotTo not implemented for composite types");
    ASSERT(!::isOtherType(code_), "castSlotTo not implemented for other types");

    const TypeCode from = code_;
    const TypeCode to   = targetType->code();

    using ::Bool;
    using ::Byte;
    using ::Float32;
    using ::Float64;
    using ::Int32;
    using ::Int64;

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
        case TypeCode::Byte:
            DO_CONV(Int32, Byte, static_cast<Byte>(v & 0xFF));
        case TypeCode::String: {
            ::String *s = ::String::from(std::to_string(v), mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotTo Int32->unsupported target");
        }
    }
    case TypeCode::Int64: {
        Int64 v = fromSlot<Int64>(static_cast<slot_t>(value));
        switch (to) {
        case TypeCode::Int32:
            DO_CONV(Int64, Int32, v);
        case TypeCode::Float32:
            DO_CONV(Int64, Float32, v);
        case TypeCode::Float64:
            DO_CONV(Int64, Float64, v);
        case TypeCode::Bool:
            DO_CONV(Int64, Bool, v != 0);
        case TypeCode::Byte:
            DO_CONV(Int64, Byte, static_cast<Byte>(v & 0xFF));
        case TypeCode::String: {
            ::String *s = ::String::from(std::to_string(v), mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotTo Int64->unsupported target");
        }
    }
    case TypeCode::Float32: {
        Float32 v = fromSlot<Float32>(static_cast<slot_t>(value));
        switch (to) {
        case TypeCode::Int32:
            DO_CONV(Float32, Int32, v);
        case TypeCode::Int64:
            DO_CONV(Float32, Int64, v);
        case TypeCode::Float64:
            DO_CONV(Float32, Float64, v);
        case TypeCode::Bool:
            DO_CONV(Float32, Bool, v != 0);
        case TypeCode::Byte:
            DO_CONV(Float32, Byte, static_cast<Byte>(static_cast<int>(v) & 0xFF));
        case TypeCode::String: {
            ::String *s = ::String::from(std::to_string(v), mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotTo Float32->unsupported target");
        }
    }
    case TypeCode::Float64: {
        Float64 v = fromSlot<Float64>(static_cast<slot_t>(value));
        switch (to) {
        case TypeCode::Int32:
            DO_CONV(Float64, Int32, v);
        case TypeCode::Int64:
            DO_CONV(Float64, Int64, v);
        case TypeCode::Float32:
            DO_CONV(Float64, Float32, v);
        case TypeCode::Bool:
            DO_CONV(Float64, Bool, v != 0);
        case TypeCode::Byte:
            DO_CONV(Float64, Byte, static_cast<Byte>(static_cast<int>(v) & 0xFF));
        case TypeCode::String: {
            ::String *s = ::String::from(std::to_string(v), mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotTo Float64->unsupported target");
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
        case TypeCode::Byte:
            DO_CONV(Bool, Byte, v ? static_cast<Byte>(1) : static_cast<Byte>(0));
        case TypeCode::String: {
            ::String *s = ::String::from(v ? "true" : "false", mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotTo Bool->unsupported target");
        }
    }
    case TypeCode::Byte: {
        Byte v = fromSlot<Byte>(static_cast<slot_t>(value));
        switch (to) {
        case TypeCode::Int32:
            DO_CONV(Byte, Int32, static_cast<int>(v));
        case TypeCode::Int64:
            DO_CONV(Byte, Int64, static_cast<int>(v));
        case TypeCode::Float32:
            DO_CONV(Byte, Float32, static_cast<int>(v));
        case TypeCode::Float64:
            DO_CONV(Byte, Float64, static_cast<int>(v));
        case TypeCode::Bool:
            DO_CONV(Byte, Bool, v != static_cast<Byte>(0));
        case TypeCode::String: {
            std::ostringstream oss;
            oss << "0x" << std::hex << static_cast<uint64_t>(v);
            ::String *s = ::String::from(oss.str(), mm::autoSpace());
            return toSlot(s);
        }
        default:
            ASSERT(false, "castSlotTo Byte->unsupported target");
        }
    }
    case TypeCode::Void:
        if (to == TypeCode::String) {
            ::String *s = ::String::from("null", mm::autoSpace());
            return toSlot(s);
        }
        ASSERT(false, "castSlotTo Void->unsupported target");
    case TypeCode::String: {
        ::String *s = reinterpret_cast<::String *>(static_cast<uintptr_t>(value));
        switch (to) {
        case TypeCode::Int32: {
            try {
                Int32 v = std::stoi(s->toString());
                return toSlot(v);
            } catch (...) {
                ASSERT(false, "castSlotTo String->Int32 parse failed");
            }
        }
        case TypeCode::Int64: {
            try {
                Int64 v = std::stoll(s->toString());
                return toSlot(v);
            } catch (...) {
                ASSERT(false, "castSlotTo String->Int64 parse failed");
            }
        }
        case TypeCode::Float32: {
            try {
                Float32 v = std::stof(s->toString());
                return toSlot(v);
            } catch (...) {
                ASSERT(false, "castSlotTo String->Float32 parse failed");
            }
        }
        case TypeCode::Float64: {
            try {
                Float64 v = std::stod(s->toString());
                return toSlot(v);
            } catch (...) {
                ASSERT(false, "castSlotTo String->Float64 parse failed");
            }
        }
        case TypeCode::String:
            return value;
        default:
            ASSERT(false, "castSlotTo String->unsupported target");
        }
    }
    default:
        ASSERT(false, "castSlotTo unsupported source type");
    }

#undef DO_CONV
    return 0; // unreachable
}

bool Type::assignable(Type *type) const {
    if (!type)
        return false;
    if (this == type)
        return true;

    // 内置类型，含有 Ref 类型的复合类型必须 resolve 后才能赋值给其他类型
    ASSERT(code_ != TypeCode::Ref && type->code_ != TypeCode::Ref, "Ref type cannot be assigned");

    // Void 类型可以接受任何类型的赋值，但不可以赋值给其他类型
    if (type->code_ == TypeCode::Void)
        return true;
    if (code_ == TypeCode::Void)
        return false;

    // 任何类型可赋给 Any（目标为 Any 时接受任意来源）
    if (type->code_ == TypeCode::Any)
        return true;
    // Any 不可赋给其他类型（来源为 Any 且目标非 Any）
    if (code_ == TypeCode::Any)
        return false;

    // 复合类型需要重载 assignable 方法处理
    if (::isComposite(code_)) {
        const auto &self = static_cast<const CompositeType &>(*this);
        return self.assignable(type);
    }

    // 第三方类型交由第三方自己重载的 assignable 方法处理
    if (::isOtherType(code_)) {
        const auto &self = static_cast<const OtherType &>(*this);
        return self.assignable(type);
    }

    // 剩余情况只能是基础类型之间的赋值，必须相同
    return code_ == type->code_;
}

Type *Type::create(TypeCode code) {
    EXEC_WHEN_DEBUG(l.in("Type").debug(
        "Allocating Type: {}, size: {} bytes",
        typeCodeToString(code),
        sizeof(Type)));
    void *mem = mm::permSpace().alloc(sizeof(Type), alignof(Type));
    ASSERT(mem != nullptr, "Failed to allocate Type from permSpace");
    return new (mem) Type(code);
}

Type *Type::Int32() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::Int32);
    }
    return type;
}

Type *Type::Int64() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::Int64);
    }
    return type;
}

Type *Type::Int() { return Type::Int64(); }

Type *Type::Float32() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::Float32);
    }
    return type;
}

Type *Type::Float64() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::Float64);
    }
    return type;
}

Type *Type::Float() { return Type::Float64(); }

Type *Type::Bool() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::Bool);
    }
    return type;
}

Type *Type::Byte() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::Byte);
    }
    return type;
}

Type *Type::Void() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::Void);
    }
    return type;
}

Type *Type::String() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::String);
    }
    return type;
}

Type *Type::Ref() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::Ref);
    }
    return type;
}

Type *Type::Any() {
    static Type *type = nullptr;
    if (type == nullptr) {
        type = Type::create(TypeCode::Any);
    }
    return type;
}
