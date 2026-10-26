// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Element type helpers shared by kernels, type inference, and serialization.
 *
 * There is no normalization here (design/arrays-memory plan 5.2) and no promotion between
 * element types: a tensor's element type is the one the program wrote, so `f64[..]` is f64 and
 * `i32[..]` is i32, and two operands that state different element types are written with the
 * conversion that says which one they compute in (design principles 5.1).
 */

#include "dtype.h"

namespace camel::tensor {

using type::TypeCode;

size_t elementSize(TypeCode dtype) {
    return dispatchDType(dtype, []<typename T>() { return sizeof(T); });
}

bool isSupportedTensorScalar(TypeCode code) {
    return code == TypeCode::Int32 || code == TypeCode::Int64 || code == TypeCode::Float32 ||
           code == TypeCode::Float64 || code == TypeCode::Bool || code == TypeCode::BFloat16 ||
           code == TypeCode::Float16;
}

bool isFloatingTensorType(TypeCode code) {
    return code == TypeCode::Float32 || code == TypeCode::Float64 ||
           code == TypeCode::BFloat16 || code == TypeCode::Float16;
}

bool isReducedFloatTensorType(TypeCode code) {
    return code == TypeCode::BFloat16 || code == TypeCode::Float16;
}

std::string elementTypeSpelling(TypeCode code) {
    switch (code) {
    case TypeCode::Float32:
        return "f32";
    case TypeCode::Float64:
        return "f64";
    case TypeCode::BFloat16:
        return "bf16";
    case TypeCode::Float16:
        return "f16";
    case TypeCode::Int32:
        return "i32";
    case TypeCode::Int64:
        return "i64";
    case TypeCode::Bool:
        return "bool";
    default:
        return type::typeCodeToString(code);
    }
}

std::string_view dtypeName(TypeCode dtype) {
    switch (dtype) {
    case TypeCode::Float32:
        return "float32";
    case TypeCode::Float64:
        return "float64";
    case TypeCode::BFloat16:
        return "bfloat16";
    case TypeCode::Float16:
        return "float16";
    case TypeCode::Int32:
        return "int32";
    case TypeCode::Int64:
        return "int64";
    case TypeCode::Bool:
        return "bool";
    default:
        return "unknown";
    }
}

std::string supportedElementTypes(std::initializer_list<TypeCode> codes) {
    std::string text;
    size_t index = 0;
    for (TypeCode code : codes) {
        if (index > 0) {
            text += index + 1 == codes.size() ? " and " : ", ";
        }
        text += elementTypeSpelling(code);
        ++index;
    }
    return text;
}

} // namespace camel::tensor