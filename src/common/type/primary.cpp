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
 * Created: Oct. 6, 2024
 * Updated: Oct. 07, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "primary.h"

TypeConv PrimaryType::convertibility(const Type &other) const {
    const TypeCode otherCode = other.code();
    if (otherCode == code_) {
        return TypeConv::SAFE;
    }
    if (other.primary()) {
        const int thisIndex = static_cast<int>(code_) & 0b00'000111;
        const int otherIndex = static_cast<int>(otherCode) & 0b00'000111;
        return static_cast<TypeConv>(primeTypeConvMatrix[thisIndex][otherIndex]);
    }
    if (other.structured()) {
        switch (otherCode) {
        case TypeCode::UNION:
            [[fallthrough]];
        case TypeCode::LIST:
            [[fallthrough]];
        case TypeCode::ARRAY:
            [[fallthrough]];
        case TypeCode::VECTOR:
            [[fallthrough]];
        case TypeCode::TENSOR:
            [[fallthrough]];
        case TypeCode::SET:
            return TypeConv::SAFE;
        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.special()) {
        switch (otherCode) {
        case TypeCode::ANY:
            return TypeConv::SAFE;
        case TypeCode::VOID:
            return TypeConv::UNSAFE;
        default:
            return TypeConv::FORBIDDEN;
        }
    }
    return TypeConv::FORBIDDEN;
}
