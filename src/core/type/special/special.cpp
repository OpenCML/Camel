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

#include "special.h"

std::string SpecialType::mangle() const {
    switch (code_) {
    case TypeCode::Any:
        return "a";
    case TypeCode::Void:
        return "v";
    case TypeCode::Function:
        ASSERT(false, "Function type mangle should be implemented in FunctionType.");
        return "";
    default:
        break;
    }
    ASSERT(false, "Unknown SpecialType");
    return ""; // unknown
}

CastSafety SpecialType::castSafetyTo(const Type &other) const {
    if (other.code() == code_) {
        return CastSafety::Safe;
    }
    if (other.code() == TypeCode::Any) {
        return CastSafety::Safe;
    }
    if (other.primary() || other.composed()) {
        return CastSafety::Forbidden;
    }
    if (other.code() == TypeCode::Void) {
        return CastSafety::Unsafe;
    }
    return CastSafety::Forbidden;
}
