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
 * Updated: Oct. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "base.h"

#include "primary.h"
#include "special/func.h"
#include "utils/type.h"

using namespace std;

const signed char primeTypeConvMatrix[7][7] = {
    // Int32, Int64, Float, Double, String, Bool, Char
    {01, 01, 01, 01, 00, 01, -1}, // Int32
    {-1, 01, -1, 01, 00, 01, -1}, // Int64
    {-1, -1, 01, 01, 00, 01, -1}, // Float
    {-1, -1, -1, 01, 00, 01, -1}, // Double
    {00, 00, 00, 00, 01, 01, 00}, // String
    {01, 01, 01, 01, 01, 01, 01}, // Bool
    {01, 01, 01, 01, 01, 01, 01}  // Char
};

string typeCodeToString(TypeCode code) {
    switch (code) {
        // internal use
    case TypeCode::Ref:
        return "ref";
        // primitive types
    case TypeCode::Int32:
        return "int32";
    case TypeCode::Int64:
        return "int64";
    case TypeCode::Float:
        return "float";
    case TypeCode::Double:
        return "double";
    case TypeCode::String:
        return "string";
    case TypeCode::Bool:
        return "bool";
    case TypeCode::Char:
        return "char";
        // composed types
    case TypeCode::Array:
        return "array";
    case TypeCode::Tuple:
        return "tuple";
    case TypeCode::Union:
        return "union";
    case TypeCode::Struct:
        return "struct";
    case TypeCode::Function:
        return "function";
        // special types
    case TypeCode::Any:
        return "any";
    case TypeCode::Void:
        return "void";
        // other types
    default:
        if ((static_cast<uint32_t>(code) & 0xF0000000) == 0xF0000000) {
            return "other";
        }
        break;
    }
    return "Unknown";
}

Type::Type(TypeCode type) : code_(type) {}

const TypeCode &Type::code() const { return code_; }

bool Type::internal() const { return (static_cast<uint32_t>(code_) & 0xF0000000) == 0x00000000; }

bool Type::primary() const { return (static_cast<uint32_t>(code_) & 0xF0000000) == 0x10000000; }

bool Type::composed() const { return (static_cast<uint32_t>(code_) & 0xF0000000) == 0x20000000; }

bool Type::special() const { return (static_cast<uint32_t>(code_) & 0xF0000000) == 0x30000000; }

bool Type::other() const { return (static_cast<uint32_t>(code_) & 0xF0000000) == 0xF0000000; }

std::string Type::toString() const { return typeCodeToString(code_); }

bool Type::equals(const type_ptr_t &type) const { return type && *type == *this; }

bool Type::assignable(const type_ptr_t &type) const {
    if (!type)
        return false;
    if (this == type.get())
        return true;

    ASSERT(code_ != TypeCode::Ref && type->code_ != TypeCode::Ref, "Ref type cannot be assigned");
    if (type->code_ == TypeCode::Any)
        return true;
    if (code_ == TypeCode::Any)
        return false;
    if (code_ == TypeCode::Void || type->code_ == TypeCode::Void)
        return false;

    if (code_ == TypeCode::Function && type->code_ == TypeCode::Function) {
        // TODO: 这里需要进一步设计
        return true;
    }
    if (composed() && type->composed()) {
        return this->equals(type);
    }

    return code_ == type->code_;
}

CastSafety Type::castSafetyTo(const Type &other) const {
    if (code_ == other.code_) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool Type::castSafetyCheck(const Type &from, const Type &to, CastSafety required) {
    CastSafety safety = from.castSafetyTo(to);
    if (required == CastSafety::Safe) {
        return safety == CastSafety::Safe;
    } else if (required == CastSafety::Unsafe) {
        return safety != CastSafety::Forbidden;
    } else { // required == CastSafety::Forbidden
        return safety == CastSafety::Forbidden;
    }
}

bool Type::castSafetyCheck(const type_ptr_t &from, const type_ptr_t &to, CastSafety required) {
    return castSafetyCheck(*from, *to, required);
}

type_ptr_t Type::Int32() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<PrimaryType>(TypeCode::Int32));
    }
    return type;
}

type_ptr_t Type::Int64() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<PrimaryType>(TypeCode::Int64));
    }
    return type;
}

type_ptr_t Type::Float() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<PrimaryType>(TypeCode::Float));
    }
    return type;
}

type_ptr_t Type::Double() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<PrimaryType>(TypeCode::Double));
    }
    return type;
}

type_ptr_t Type::String() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<PrimaryType>(TypeCode::String));
    }
    return type;
}

type_ptr_t Type::Bool() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<PrimaryType>(TypeCode::Bool));
    }
    return type;
}

type_ptr_t Type::Char() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<PrimaryType>(TypeCode::Char));
    }
    return type;
}

type_ptr_t Type::Int() { return Int32(); }

type_ptr_t Type::Real() { return Float(); }

type_ptr_t Type::Number() { return Double(); }

type_ptr_t Type::List() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<ListType>());
    }
    return type;
}

type_ptr_t Type::Array(const type_ptr_t &elementType) {
    return tt::as_shared<Type>(make_shared<ArrayType>(elementType));
}

type_ptr_t Type::Tuple(const type_vec_t &types) {
    return tt::as_shared<Type>(make_shared<TupleType>(types));
}

type_ptr_t Type::Vector(const type_ptr_t &elementType, size_t size) {
    return tt::as_shared<Type>(make_shared<VectorType>(elementType, size));
}

type_ptr_t Type::Any() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<SpecialType>(TypeCode::Any));
    }
    return type;
}

type_ptr_t Type::Void() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<SpecialType>(TypeCode::Void));
    }
    return type;
}

type_ptr_t Type::Func() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type =
            make_shared<FunctionType>(param_vec_t{}, param_vec_t{}, Type::Void(), Modifier::None);
    }
    return type;
}

type_ptr_t Type::Ref() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = tt::as_shared<Type>(make_shared<SpecialType>(TypeCode::Ref));
    }
    return type;
}
