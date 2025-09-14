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

#include "primary.h"

CastSafety PrimaryType::castSafetyTo(const Type &other) const {
    const TypeCode otherCode = other.code();
    if (otherCode == code_) {
        return CastSafety::Safe;
    }
    if (other.primary()) {
        const int thisIndex = static_cast<int>(code_) & 0b00'000111;
        const int otherIndex = static_cast<int>(otherCode) & 0b00'000111;
        return static_cast<CastSafety>(primeTypeConvMatrix[thisIndex][otherIndex]);
    }
    if (other.structured()) {
        switch (otherCode) {
        case TypeCode::Union:
            [[fallthrough]];
        case TypeCode::List:
            [[fallthrough]];
        case TypeCode::Array:
            [[fallthrough]];
        case TypeCode::Vector:
            [[fallthrough]];
        case TypeCode::Tensor:
            [[fallthrough]];
        case TypeCode::Set:
            return CastSafety::Safe;
        default:
            return CastSafety::Forbidden;
        }
    }
    if (other.special()) {
        switch (otherCode) {
        case TypeCode::Any:
            return CastSafety::Safe;
        case TypeCode::Void:
            return CastSafety::Unsafe;
        default:
            return CastSafety::Forbidden;
        }
    }
    return CastSafety::Forbidden;
}
