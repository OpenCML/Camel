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
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "base.h"

#include "composite/composite.h"
#include "core/mm/mm.h"
#include "other.h"
#include "utils/type.h"

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
    case TypeCode::Frame:
        return "frame";
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
        return "i";
    case TypeCode::Int64:
        return "l";
    case TypeCode::Float32:
        return "f";
    case TypeCode::Float64:
        return "d";
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

CastSafety Type::castSafetyTo(const Type &other) const {
    ASSERT(!::isComposite(code_), "Composite type cannot call Type::castSafetyTo");
    ASSERT(!::isOtherType(code_), "Other type cannot call Type::castSafetyTo");

    // 这里要先排除掉辅助类型
    if (::isAuxiliary(code_) || ::isAuxiliary(other.code())) {
        return CastSafety::Forbidden;
    }

    // 排除掉辅助类型后，原始类型只剩8个
    if (::isPrimitive(code_) && ::isPrimitive(other.code())) {
        const int thisIndex  = static_cast<int>(code_) & 0b1111;
        const int otherIndex = static_cast<int>(other.code()) & 0b1111;
        return static_cast<CastSafety>(primitiveTypeConvMatrix[thisIndex][otherIndex]);
    }

    // 剩余所有类型默认不允许转换
    return CastSafety::Forbidden;
}

bool Type::assignable(Type *type) const {
    if (!type)
        return false;
    if (this == type)
        return true;

    // 内置类型，含有 Ref 类型的复合类型必须 resolve 后才能赋值给其他类型
    ASSERT(code_ != TypeCode::Ref && type->code_ != TypeCode::Ref, "Ref type cannot be assigned");

    // any 类型可以接受任何类型的赋值，但不可以赋值给其他类型
    if (type->code_ == TypeCode::Any)
        return true;
    if (code_ == TypeCode::Any)
        return false;

    // Void 类型可以接受任何类型的赋值，但不可以赋值给其他类型
    if (type->code_ == TypeCode::Void)
        return true;
    if (code_ == TypeCode::Void)
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
