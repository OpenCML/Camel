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
 * Created: Nov. 07, 2025
 * Updated: Nov. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "base.h"

#include "array.h"
#include "string.h"

DataLayout getLayoutOfType(TypeCode code) {
    switch (code) {
    case TypeCode::Int:
        return {sizeof(Int), alignof(Int)};
    case TypeCode::Long:
        return {sizeof(Long), alignof(Long)};
    case TypeCode::Float:
        return {sizeof(Float), alignof(Float)};
    case TypeCode::Double:
        return {sizeof(Double), alignof(Double)};
    case TypeCode::Bool:
        return {sizeof(Bool), alignof(Bool)};
    case TypeCode::Byte:
        return {sizeof(Byte), alignof(Byte)};
    case TypeCode::String:
        return {sizeof(GCString), alignof(GCString)};
    default:
        throw std::invalid_argument("Invalid TypeCode");
    }
}
