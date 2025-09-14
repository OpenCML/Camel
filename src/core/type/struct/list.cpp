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

#include "list.h"

using namespace std;

ListType::ListType() : StructType(TypeCode::List) {}

string ListType::toString() const { return "List"; }

bool ListType::operator==(const Type &other) const { return true; }

bool ListType::operator!=(const Type &other) const { return false; }

CastSafety ListType::castSafetyTo(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::List:
            return CastSafety::Safe;
        case TypeCode::Set:
            [[fallthrough]];
        case TypeCode::Map:
            [[fallthrough]];
        case TypeCode::Array:
            [[fallthrough]];
        case TypeCode::Dict:
            [[fallthrough]];
        case TypeCode::Union:
            [[fallthrough]];
        case TypeCode::Vector:
            [[fallthrough]];
        case TypeCode::Tensor:
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
