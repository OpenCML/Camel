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
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "base.h"

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
    case TypeCode::Params:
        return "Params";
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

TypeConv Type::convertibility(const Type &other) const {
    if (code_ == other.code_) {
        return TypeConv::SAFE;
    }
    return TypeConv::FORBIDDEN;
}
