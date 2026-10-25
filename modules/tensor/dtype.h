// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Tensor element types.
 *
 * A tensor is an array of one scalar element type: f32, f64, bf16, f16, i32, i64 or bool.
 * The element type a program writes is the one the tensor stores (design
 * principles 5.1, "no implicit conversions between numeric types";
 * design/arrays-memory plan 5.2, "element type exact"): `f64[..]` holds
 * f64 and `i32[..]` holds i32. There is no storage dtype that a written
 * type is replaced with.
 *
 * bf16 and f16 are stored as two bytes (camel/core/rtdata/half.h) and are written only by a
 * conversion that rounds into them. A conversion the program writes (`cast`) is the precision it
 * asked for and needs no permission: `change_precision` governs a computation the compiler moves
 * to lower precision (design principles 7.2-7.3). Every kernel
 * computes a bf16 or f16 element in f32 and rounds it once when it stores it: the same one
 * arithmetic path for every elementwise operator, stated in `elementwise.cpp` and pinned by the
 * dtype test plan.
 *
 * Kernels never branch on the element type per element: `dispatchDType`
 * selects a typed instantiation once per call. A kernel that supports only
 * some element types says so where it is called and refuses the others with
 * `RuntimeDiag::OperandTypeUnsupported`, which names the operator, the
 * argument's element type and the set the kernel takes; it never converts
 * silently.
 */

#pragma once

#include "camel/core/rtdata/half.h"
#include "camel/core/type/base.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
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

/// True for the floating element types (f32, f64, bf16, f16).
bool isFloatingTensorType(type::TypeCode code);

/// True for the reduced-precision floating element types (bf16, f16): two bytes per element,
/// written by a conversion that rounds, computed in f32.
bool isReducedFloatTensorType(type::TypeCode code);

/// The element type as a program writes it in the array spelling: "f32", "f64", "bf16", "f16",
/// "i32", "i64", "bool"; other codes fall back to their source name
/// (`type::typeCodeToString`).
std::string elementTypeSpelling(type::TypeCode code);

/// NumPy-style promotion of two tensor element types, exact. Among the scalar types the order
/// is bool < i32 < i64 < f32 < f64; a bf16 or f16 promotes with f32 and f64 (to the wider one)
/// and with itself, but not with the other reduced type or an integer: there is no type that
/// holds every value of both, so the call is refused and the program casts one operand to the
/// element type the result should have. The result of a promotion that succeeds is one of the
/// two operand types; neither operand is first replaced by a third.
type::TypeCode promoteTensorTypes(type::TypeCode lhs, type::TypeCode rhs);

/// Human-readable element type name ("float32", "float64", "bfloat16", "float16", "int32",
/// "int64", "bool"): the storage name the tensor print and the intern key use.
std::string_view dtypeName(type::TypeCode dtype);

/// The element types a kernel takes, written for `RuntimeDiag::OperandTypeUnsupported`:
/// "f32 and i64", "f32, i64 and bool".
std::string supportedElementTypes(std::initializer_list<type::TypeCode> codes);

/// Maps a storage dtype to the matching C++ element type.
template <type::TypeCode Code> struct StorageOf;
template <> struct StorageOf<type::TypeCode::Float32> {
    using type = float;
};
template <> struct StorageOf<type::TypeCode::Float64> {
    using type = double;
};
template <> struct StorageOf<type::TypeCode::Int32> {
    using type = int32_t;
};
template <> struct StorageOf<type::TypeCode::Int64> {
    using type = int64_t;
};
template <> struct StorageOf<type::TypeCode::Bool> {
    using type = bool_t;
};
template <> struct StorageOf<type::TypeCode::BFloat16> {
    using type = camel::core::rtdata::BFloat16;
};
template <> struct StorageOf<type::TypeCode::Float16> {
    using type = camel::core::rtdata::Float16;
};

/**
 * Invokes `fn.template operator()<T>()` with T the storage type of `dtype`.
 * All typed kernels are instantiated through this single switch.
 */
template <typename Fn> decltype(auto) dispatchDType(type::TypeCode dtype, Fn &&fn) {
    switch (dtype) {
    case type::TypeCode::Float32:
        return fn.template operator()<float>();
    case type::TypeCode::Float64:
        return fn.template operator()<double>();
    case type::TypeCode::Int32:
        return fn.template operator()<int32_t>();
    case type::TypeCode::Int64:
        return fn.template operator()<int64_t>();
    case type::TypeCode::Bool:
        return fn.template operator()<bool_t>();
    case type::TypeCode::BFloat16:
        return fn.template operator()<camel::core::rtdata::BFloat16>();
    case type::TypeCode::Float16:
        return fn.template operator()<camel::core::rtdata::Float16>();
    default:
        throw std::invalid_argument("Unsupported array element type");
    }
}

/// Storage dtype code of a C++ element type.
template <typename T> constexpr type::TypeCode dtypeOf() {
    if constexpr (std::is_same_v<T, float>) {
        return type::TypeCode::Float32;
    } else if constexpr (std::is_same_v<T, double>) {
        return type::TypeCode::Float64;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return type::TypeCode::Int32;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return type::TypeCode::Int64;
    } else if constexpr (std::is_same_v<T, camel::core::rtdata::BFloat16>) {
        return type::TypeCode::BFloat16;
    } else if constexpr (std::is_same_v<T, camel::core::rtdata::Float16>) {
        return type::TypeCode::Float16;
    } else {
        static_assert(std::is_same_v<T, bool_t>, "Unsupported tensor element type");
        return type::TypeCode::Bool;
    }
}

} // namespace camel::tensor