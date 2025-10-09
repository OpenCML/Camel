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
 * Updated: Oct. 05, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "base.h"

#include "primary.h"
#include "special/func.h"
#include "struct/list.h"
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
        // structured types
    case TypeCode::Set:
        return "Set";
    case TypeCode::Map:
        return "Map";
    case TypeCode::Dict:
        return "Dict";
    case TypeCode::List:
        return "List";
    case TypeCode::Union:
        return "Union";
    case TypeCode::Array:
        return "Array";
    case TypeCode::Tuple:
        return "Tuple";
    case TypeCode::Vector:
        return "Vector";
    case TypeCode::Tensor:
        return "Tensor";
        // special types
    case TypeCode::Any:
        return "any";
    case TypeCode::Void:
        return "void";
    case TypeCode::Func:
        return "functor";
    case TypeCode::Ref:
        return "REF";
    }
    return "Unknown";
}

Type::Type(TypeCode type) : code_(type) {}

const TypeCode &Type::code() const { return code_; }

bool Type::primary() const { return (static_cast<int>(code_) & 0b11'000000) == 0b00'000000; }

bool Type::structured() const { return (static_cast<int>(code_) & 0b11'000000) == 0b01'000000; }

bool Type::special() const { return (static_cast<int>(code_) & 0b11'000000) == 0b10'000000; }

std::string Type::toString() const { return typeCodeToString(code_); }

bool Type::operator==(const Type &other) const { return code_ == other.code_; }

bool Type::operator!=(const Type &other) const { return code_ != other.code_; }

bool Type::equals(const type_ptr_t &type) const { return type && *type == *this; }

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

type_ptr_t Type::Array(const type_ptr_t &elementType, size_t size) {
    return tt::as_shared<Type>(make_shared<ArrayType>(elementType, size));
}

type_ptr_t Type::Tuple(const std::vector<type_ptr_t> &types) {
    return tt::as_shared<Type>(make_shared<TupleType>(types));
}

type_ptr_t Type::Vector(const type_ptr_t &elementType) {
    return tt::as_shared<Type>(make_shared<VectorType>(elementType));
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
        type = tt::as_shared<Type>(
            make_shared<FunctionType>(param_init_list_t{}, param_init_list_t{}, Any()));
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
