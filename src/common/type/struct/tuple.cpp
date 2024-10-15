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

#include "tuple.h"

using namespace std;

TupleType::TupleType() : StructType(TypeCode::TUPLE) {}

TupleType::TupleType(const initializer_list<type_ptr_t> &types) : StructType(TypeCode::TUPLE), types_(types) {}

TupleType::TupleType(const vector<type_ptr_t> &types) : StructType(TypeCode::TUPLE), types_(types) {}

string TupleType::toString() const {
    string result = "Tuple<";
    for (const auto &type : types_) {
        if (type) {
            result += type->toString() + ", ";
        } else {
            result += "NULL, ";
        }
    }
    if (!types_.empty()) {
        result.pop_back();
        result.pop_back();
    }
    result += ">";
    return result;
}

bool TupleType::operator==(const Type &other) const {
    if (other.code() != TypeCode::TUPLE) {
        return false;
    }
    const TupleType &otherTuple = dynamic_cast<const TupleType &>(other);

    if (types_.size() != otherTuple.types_.size()) {
        return false;
    }
    for (size_t i = 0; i < types_.size(); i++) {
        if (!types_[i]->equals(otherTuple.types_[i])) {
            return false;
        }
    }
    return true;
}
bool TupleType::operator!=(const Type &other) const { return !(*this == other); }

void TupleType::add(const type_ptr_t &type) { types_.push_back(type); }

void TupleType::set(size_t index, const type_ptr_t &type) { types_[index] = type; }

size_t TupleType::size() const { return types_.size(); }

const vector<type_ptr_t> &TupleType::types() const { return types_; }

TypeConv TupleType::convertibility(const Type &other) const {
    // TODO: not fully implemented
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::TENSOR:
            return TypeConv::SAFE;
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET:
            return TypeConv::SAFE;
        case TypeCode::ARRAY:
            return TypeConv::SAFE;
        case TypeCode::VECTOR:
            return TypeConv::SAFE;
        case TypeCode::MAP:
            return TypeConv::SAFE;
        case TypeCode::DICT:
            return TypeConv::SAFE;
        case TypeCode::UNION:
            return TypeConv::SAFE;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primary types and special types are forbidden
    return TypeConv::FORBIDDEN;
}
