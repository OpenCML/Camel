// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Element type helpers shared by kernels, type inference, and serialization.
 *
 * There is no normalization here (design/arrays-memory plan 5.2): a tensor's
 * element type is the one the program wrote, so `f64[..]` is f64 and
 * `i32[..]` is i32. Promotion only picks one of the two operand types.
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

TypeCode promoteTensorTypes(TypeCode lhs, TypeCode rhs) {
    if (!isSupportedTensorScalar(lhs)) {
        throw std::invalid_argument("Unsupported array element type");
    }
    if (!isSupportedTensorScalar(rhs)) {
        throw std::invalid_argument("Unsupported array element type");
    }
    // A reduced-precision type meets the wider float it is a subset of, or its own type. It
    // never meets the other reduced type or an integer: neither type holds every value of the
    // other (bf16 has f32's range with 8 significand bits, f16 has 11; an i64 has 63), so a
    // promotion would have to invent a result type or narrow silently. The program casts one
    // operand; that written conversion is the precision it asked for, so no numeric permission is
    // involved (design principles 7.2-7.3).
    if (isReducedFloatTensorType(lhs) || isReducedFloatTensorType(rhs)) {
        if (lhs == rhs) {
            return lhs;
        }
        const auto widerFloat = [](TypeCode reduced, TypeCode other) {
            return isReducedFloatTensorType(reduced) &&
                   (other == TypeCode::Float32 || other == TypeCode::Float64);
        };
        if (widerFloat(lhs, rhs)) {
            return rhs;
        }
        if (widerFloat(rhs, lhs)) {
            return lhs;
        }
        throw std::invalid_argument(
            "cannot mix '" + elementTypeSpelling(lhs) + "' and '" + elementTypeSpelling(rhs) +
            "' elements: neither type holds every value of the other. Cast one of them to the "
            "element type the result should have");
    }
    // Widening only: the result is one of the two element types, so promotion never
    // replaces an operand's type with a third one. f64 > f32 > i64 > i32 > bool.
    //
    // The two float widths are the one pair this ladder would widen silently, so the declaration
    // that takes two tensors refuses them first (refuseMixedFloatWidths): a tensor operator
    // computes in its element type, and an f32 tensor beside an f64 one is written with the
    // conversion that says which width it computes in (design principles 5.1). An integer or bool
    // operand is read at the float tensor's element type, here as everywhere (a bool's arithmetic
    // has always been f32, and two integers widen exactly), and a reduced-precision type meeting a
    // wider float is the rule above.
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