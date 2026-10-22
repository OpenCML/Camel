// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Element type helpers shared by kernels, type inference, and serialization.
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

std::optional<TypeCode> storedTensorDType(TypeCode code) {
    switch (code) {
    case TypeCode::Float64:
        return TypeCode::Float32;
    case TypeCode::Int32:
        return TypeCode::Int64;
    default:
        return std::nullopt;
    }
}

TypeCode normalizeTensorDType(TypeCode code) {
    if (const std::optional<TypeCode> stored = storedTensorDType(code)) {
        return *stored;
    }
    switch (code) {
    case TypeCode::Float32:
    case TypeCode::Int64:
    case TypeCode::Bool:
        return code;
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
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
    lhs = normalizeTensorDType(lhs);
    rhs = normalizeTensorDType(rhs);
    if (lhs == TypeCode::Float32 || rhs == TypeCode::Float32) {
        return TypeCode::Float32;
    }
    if (lhs == TypeCode::Int64 || rhs == TypeCode::Int64) {
        return TypeCode::Int64;
    }
    return TypeCode::Bool;
}

std::string_view dtypeName(TypeCode dtype) {
    switch (dtype) {
    case TypeCode::Float32:
        return "float32";
    case TypeCode::Int64:
        return "int64";
    case TypeCode::Bool:
        return "bool";
    default:
        return "unknown";
    }
}

} // namespace camel::tensor
