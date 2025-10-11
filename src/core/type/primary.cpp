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

#include "primary.h"

bool PrimaryType::operator==(const Type &other) const {
    if (!other.primary()) {
        return false;
    }
    return code_ == other.code();
}

bool PrimaryType::operator!=(const Type &other) const {
    if (!other.primary()) {
        return false;
    }
    return code_ == other.code();
}

std::string PrimaryType::mangle() const {
    switch (code_) {
    case TypeCode::Int32:
        return "i";
    case TypeCode::Int64:
        return "l";
    case TypeCode::Float:
        return "f";
    case TypeCode::Double:
        return "d";
    case TypeCode::String:
        return "s";
    case TypeCode::Bool:
        return "b";
    case TypeCode::Char:
        return "c";
    default:
        ASSERT(false, "Unknown PrimaryType");
        return "";
    }
    ASSERT(false, "Unknown PrimaryType");
    return ""; // unknown
}

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
    if (other.composed()) {
        switch (otherCode) {
        case TypeCode::Union:
            [[fallthrough]];
        case TypeCode::Array:
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
