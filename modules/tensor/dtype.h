// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Tensor element types.
 *
 * Tensors store one of three element types: float32, int64, and bool (one
 * byte per element). Camel scalar types map onto them (float/double ->
 * float32, int/long -> int64). Kernels never branch on the element type per
 * element: `dispatchDType` selects a typed instantiation once per call.
 *
 * A tensor whose declared element type is not one of the three is refused at
 * the type expression (see `storedTensorDType`): it is never built as the
 * type the declaration would have been replaced with. Element-type-exact
 * storage is design/arrays-memory P4 and is not implemented yet.
 */

#pragma once

#include "camel/core/type/base.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace camel::tensor {

namespace type = camel::core::type;

/// Storage type used for bool tensors. One byte per element, 0 or 1.
using bool_t = uint8_t;

/// Bytes per element of a storage dtype.
size_t elementSize(type::TypeCode dtype);

/// True for scalar type codes that can be used as tensor elements or operands.
bool isSupportedTensorScalar(type::TypeCode code);

/// True for the floating storage dtype.
bool isFloatingTensorType(type::TypeCode code);

/// The storage dtype a tensor of `code` would get when that is not `code` itself: f64 would be
/// stored as f32 and i32 as i64. Empty when the code is the dtype the tensor stores. The type
/// expression is refused with this answer, so the substitution is never silent.
std::optional<type::TypeCode> storedTensorDType(type::TypeCode code);

/// Maps a scalar or storage type code to its storage dtype. Throws for non-numeric codes.
type::TypeCode normalizeTensorDType(type::TypeCode code);

/// The element type as a program writes it inside `Tensor<...>`: "f32", "f64", "i32", "i64",
/// "bool"; other codes fall back to their source name (`type::typeCodeToString`).
std::string elementTypeSpelling(type::TypeCode code);

/// NumPy-style promotion restricted to the storage dtypes: bool < int64 < float32.
type::TypeCode promoteTensorTypes(type::TypeCode lhs, type::TypeCode rhs);

/// Human-readable storage dtype name ("float32", "int64", "bool").
std::string_view dtypeName(type::TypeCode dtype);

/// Maps a storage dtype to the matching C++ element type.
template <type::TypeCode Code> struct StorageOf;
template <> struct StorageOf<type::TypeCode::Float32> {
    using type = float;
};
template <> struct StorageOf<type::TypeCode::Int64> {
    using type = int64_t;
};
template <> struct StorageOf<type::TypeCode::Bool> {
    using type = bool_t;
};

/**
 * Invokes `fn.template operator()<T>()` with T the storage type of `dtype`.
 * All typed kernels are instantiated through this single switch.
 */
template <typename Fn> decltype(auto) dispatchDType(type::TypeCode dtype, Fn &&fn) {
    switch (dtype) {
    case type::TypeCode::Float32:
        return fn.template operator()<float>();
    case type::TypeCode::Int64:
        return fn.template operator()<int64_t>();
    case type::TypeCode::Bool:
        return fn.template operator()<bool_t>();
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
}

/// Storage dtype code of a C++ element type.
template <typename T> constexpr type::TypeCode dtypeOf() {
    if constexpr (std::is_same_v<T, float>) {
        return type::TypeCode::Float32;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return type::TypeCode::Int64;
    } else {
        static_assert(std::is_same_v<T, bool_t>, "Unsupported tensor element type");
        return type::TypeCode::Bool;
    }
}

} // namespace camel::tensor
