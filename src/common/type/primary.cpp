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

#include "primary.h"

TypeConv PrimaryType::convertibility(const Type &other) const {
    const TypeCode otherCode = other.code();
    if (otherCode == code_) {
        return TypeConv::SAFE;
    }
    if (other.primitive()) {
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
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::DICT:
            return TypeConv::FORBIDDEN;
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
        case TypeCode::FUNCTOR:
            return TypeConv::FORBIDDEN;
        default:
            return TypeConv::FORBIDDEN;
        }
    }
    return TypeConv::FORBIDDEN;
}