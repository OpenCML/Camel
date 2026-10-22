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

TypeCode normalizeTensorDType(TypeCode code) {
    switch (code) {
    case TypeCode::Float32:
    case TypeCode::Float64:
        return TypeCode::Float32;
    case TypeCode::Int32:
    case TypeCode::Int64:
        return TypeCode::Int64;
    case TypeCode::Bool:
        return TypeCode::Bool;
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
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
