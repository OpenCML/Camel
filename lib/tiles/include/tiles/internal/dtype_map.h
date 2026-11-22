// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The two names one element type has: the language's `TypeCode` and the kernel's `DType`. Every site
// that used to write `dtype != Float32` or `width == DType::F32 ? Float32 : Float64` was a two-way
// guess between these two names, and a fifth element type breaks the guess silently. Both
// directions are exhaustive, so a value added to either enum fails to compile here rather than
// being read as its neighbour.

#pragma once

#include "camel/core/type/base.h"
#include "tiles/dtype.h"

#include <optional>

namespace camel::tiles {
/// The kernel type of a language element type, or nothing when the value is not an element type the
/// device path knows (a string, a function, a tuple).
inline std::optional<DType> dtypeOfElementType(core::type::TypeCode code) {
    switch (code) {
    case core::type::TypeCode::Float32:
        return DType::F32;
    case core::type::TypeCode::Float64:
        return DType::F64;
    case core::type::TypeCode::BFloat16:
        return DType::BF16;
    case core::type::TypeCode::Float16:
        return DType::F16;
    case core::type::TypeCode::Int64:
        return DType::I64;
    default:
        return std::nullopt;
    }
}

/// The language element type of a kernel type. Total: every kernel type is an element type of the
/// language, which is why a `DType` can be handed back to the tensor module.
inline core::type::TypeCode elementTypeOf(DType dtype) {
    switch (dtype) {
    case DType::F32:
        return core::type::TypeCode::Float32;
    case DType::F64:
        return core::type::TypeCode::Float64;
    case DType::BF16:
        return core::type::TypeCode::BFloat16;
    case DType::F16:
        return core::type::TypeCode::Float16;
    case DType::I64:
        return core::type::TypeCode::Int64;
    }
    return core::type::TypeCode::Float32;
}
} // namespace camel::tiles