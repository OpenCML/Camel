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

#include "list.h"

using namespace std;

ListType::ListType() : OtherType(typeCode()) {}

string ListType::toString() const { return "List"; }

std::string ListType::mangle() const {
    std::string result = "L";
    return result;
}

bool ListType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    return other.code() == typeCode();
}

bool ListType::operator!=(const Type &other) const { return other.code() != typeCode(); }

CastSafety ListType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    if (other.code() == typeCode()) {
        return CastSafety::Safe;
    }
    if (other.composed()) {
        switch (other.code()) {
        case TypeCode::Array:
            [[fallthrough]];
        case TypeCode::Struct:
            [[fallthrough]];
        case TypeCode::Union:
            return CastSafety::Forbidden;

        default:
            return CastSafety::Forbidden;
        }
    }
    if (other.code() == TypeCode::Any) {
        return CastSafety::Safe;
    }
    // primary types and special types are forbidden
    return CastSafety::Forbidden;
}
