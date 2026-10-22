// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Element type helpers shared by kernels, type inference, and serialization.
 *
 * There is no normalization here (design/arrays-memory plan 5.2): a tensor's
 * element type is the one the program wrote, so `Tensor<f64>` is f64 and
 * `Tensor<i32>` is i32. Promotion only picks one of the two operand types.
 */

#include "dtype.h"

namespace camel::tensor {

using type::TypeCode;

size_t elementSize(TypeCode dtype) {
    return dispatchDType(dtype, []<typename T>() { return sizeof(T); });
}

bool isSupportedTensorScalar(TypeCode code) {
    return code == TypeCode::Int32 || code == TypeCode::Int64 || code == TypeCode::Float32 ||
           code == TypeCode::Float64 || code == TypeCode::Bool;
}

bool isFloatingTensorType(TypeCode code) {
    return code == TypeCode::Float32 || code == TypeCode::Float64;
}

std::string elementTypeSpelling(TypeCode code) {
    switch (code) {
    case TypeCode::Float32:
        return "f32";
    case TypeCode::Float64:
        return "f64";
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

TypeCode promoteTensorTypes(TypeCode lhs, TypeCode rhs) {
    if (!isSupportedTensorScalar(lhs)) {
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
    if (!isSupportedTensorScalar(rhs)) {
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
    // Widening only: the result is one of the two element types, so promotion never
    // replaces an operand's type with a third one. f64 > f32 > i64 > i32 > bool.
    const auto rank = [](TypeCode code) {
        switch (code) {
        case TypeCode::Float64:
            return 4;
        case TypeCode::Float32:
            return 3;
        case TypeCode::Int64:
            return 2;
        case TypeCode::Int32:
            return 1;
        default:
            return 0;
        }
    };
    return rank(lhs) >= rank(rhs) ? lhs : rhs;
}

std::string_view dtypeName(TypeCode dtype) {
    switch (dtype) {
    case TypeCode::Float32:
        return "float32";
    case TypeCode::Float64:
        return "float64";
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