/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "type.h"

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
        return "Int32";
    case TypeCode::INT64:
        return "Int64";
    case TypeCode::FLOAT:
        return "Float";
    case TypeCode::DOUBLE:
        return "Double";
    case TypeCode::STRING:
        return "String";
    case TypeCode::BOOL:
        return "Bool";
    case TypeCode::CHAR:
        return "Char";
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
        return "Any";
    case TypeCode::VOID:
        return "Void";
    case TypeCode::FUNCTOR:
        return "Functor";
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
