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
    // INT32, INT64, FLOAT, DOUBLE, STRING, BOOL, CHAR
    {01, 01, 01, 01, 00, 01, -1}, // INT32
    {-1, 01, -1, 01, 00, 01, -1}, // INT64
    {-1, -1, 01, 01, 00, 01, -1}, // FLOAT
    {-1, -1, -1, 01, 00, 01, -1}, // DOUBLE
    {00, 00, 00, 00, 01, 01, 00}, // STRING
    {01, 01, 01, 01, 01, 01, 01}, // BOOL
    {01, 01, 01, 01, 01, 01, 01}  // CHAR
};

string typeCodeToString(TypeCode code) {
    switch (code) {
        // primitive types
    case TypeCode::INT32:
        return "int32";
    case TypeCode::INT64:
        return "int64";
    case TypeCode::FLOAT:
        return "float";
    case TypeCode::DOUBLE:
        return "double";
    case TypeCode::STRING:
        return "string";
    case TypeCode::BOOL:
        return "bool";
    case TypeCode::CHAR:
        return "char";
        // structured types
    case TypeCode::SET:
        return "Set";
    case TypeCode::MAP:
        return "Map";
    case TypeCode::DICT:
        return "Dict";
    case TypeCode::LIST:
        return "List";
    case TypeCode::UNION:
        return "Union";
    case TypeCode::ARRAY:
        return "Array";
    case TypeCode::TUPLE:
        return "Tuple";
    case TypeCode::VECTOR:
        return "Vector";
    case TypeCode::PARAMS:
        return "Params";
    case TypeCode::TENSOR:
        return "Tensor";
        // special types
    case TypeCode::ANY:
        return "any";
    case TypeCode::VOID:
        return "void";
    case TypeCode::FUNC:
        return "functor";
    case TypeCode::REF:
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
