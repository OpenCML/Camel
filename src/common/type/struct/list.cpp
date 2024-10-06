/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 6, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "list.h"

using namespace std;

ListType::ListType() : StructType(TypeCode::LIST) {}

string ListType::toString() const { return "List"; }

bool ListType::operator==(const Type &other) const { return true; }

bool ListType::operator!=(const Type &other) const { return false; }

TypeConv ListType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET:
            [[fallthrough]];
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::ARRAY:
            [[fallthrough]];
        case TypeCode::DICT:
            [[fallthrough]];
        case TypeCode::UNION:
            [[fallthrough]];
        case TypeCode::VECTOR:
            [[fallthrough]];
        case TypeCode::TENSOR:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}