// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A type name applied to an array: the elementwise conversion of a numeric array to another
 * element type (`f32(xs)`, `int(xs)`, design principles 5.1). It is part of what a type name
 * means, so it lives with the core's conversions and needs no module; the `tensor` module's
 * `cast_like` converts through the same function.
 *
 * Each element converts as the scalar conversion of its type does, except that nothing faults: a
 * float is cut towards zero by the C++ conversion, a bool is "not zero", and a conversion into
 * bf16 or f16 rounds to nearest, ties to even, once (from the other reduced type it widens to f32
 * first).
 */

#pragma once

#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/type/base.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace camel::core {

/// Invokes `fn.template operator()<T>()` with T the storage type of a numeric element type code.
template <typename Fn> decltype(auto) dispatchElementType(type::TypeCode code, Fn &&fn) {
    switch (code) {
    case type::TypeCode::Float32:
        return fn.template operator()<float>();
    case type::TypeCode::Float64:
        return fn.template operator()<double>();
    case type::TypeCode::Int32:
        return fn.template operator()<int32_t>();
    case type::TypeCode::Int64:
        return fn.template operator()<int64_t>();
    case type::TypeCode::Bool:
        return fn.template operator()<uint8_t>();
    case type::TypeCode::BFloat16:
        return fn.template operator()<rtdata::BFloat16>();
    case type::TypeCode::Float16:
        return fn.template operator()<rtdata::Float16>();
    default:
        throw std::invalid_argument(
            "the language has no array conversion to '" + type::typeCodeToString(code) +
            "': a type name converts a numeric array");
    }
}

/// `count` elements of type From converted into type To.
template <typename To, typename From>
void convertElements(const From *in, To *out, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) {
        if constexpr (std::is_same_v<To, uint8_t>) {
            out[i] = in[i] != From{} ? 1 : 0;
        } else if constexpr (rtdata::is_reduced_float_v<To> && rtdata::is_reduced_float_v<From>) {
            out[i] = To(in[i].toFloat());
        } else {
            out[i] = static_cast<To>(in[i]);
        }
    }
}

/// A new compact array of `target` elements with the input's extents and converted elements.
inline Array *convertArrayElements(
    const Array *input, type::TypeCode target, camel::core::mm::IAllocator &allocator) {
    const type::ArrayType *arrayType = type::ArrayType::create(type::Type::create(target));
    Array *out = Array::create(allocator, arrayType, input->shapeSpan(), false);
    if (target == input->dtype()) {
        if (input->byteSize() > 0) {
            std::memcpy(out->rawData(), input->rawData(), input->byteSize());
        }
        return out;
    }
    const uint64_t count = input->numel();
    dispatchElementType(target, [&]<typename To>() {
        dispatchElementType(input->dtype(), [&]<typename From>() {
            convertElements<To, From>(input->dataAs<From>(), out->dataAs<To>(), count);
        });
    });
    return out;
}

} // namespace camel::core
