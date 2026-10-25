// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a tensor's static type is, now that `Tensor` is an alias of the native array type
 * (docs/design/arrays-memory/plan.cn.md section 5): the predicate every entry point that takes a
 * tensor reads, and the facts an operator sees through it.
 */

#include "type.h"

#include "dtype.h"

#include "camel/core/type/composite/array.h"
#include "camel/utils/assert.h"

#include <optional>
#include <span>
#include <vector>

namespace camel::tensor {

using type::ArrayType;
using type::ExtentForm;
using type::Type;
using type::TypeCode;

std::vector<int64_t> extentsOf(const StaticShape &shape) {
    std::vector<int64_t> out;
    out.reserve(shape.size());
    for (const std::optional<int64_t> &extent : shape) {
        ASSERT(extent.has_value(), "a shape the caller checked is known has every extent");
        out.push_back(*extent);
    }
    return out;
}

std::vector<int64_t> kernelExtents(const StaticShape &shape) {
    std::vector<int64_t> out;
    out.reserve(shape.size());
    for (const std::optional<int64_t> &extent : shape) {
        out.push_back(extent.value_or(-1));
    }
    return out;
}

StaticShape knownShape(std::vector<int64_t> extents) {
    StaticShape out;
    out.reserve(extents.size());
    for (int64_t extent : extents) {
        out.push_back(extent < 0 ? std::nullopt : std::optional<int64_t>(extent));
    }
    return out;
}

bool isTensorType(const Type *type) {
    // A tensor is a numeric array (plan section 5.2): the name `Tensor` is a spelling of the
    // array type (`f32[2, 3]` is `f32[2, 3]`), and a numeric array a program writes
    // (`f32[2, 3]`) is a
    // tensor too. One predicate answers for both spellings, and every entry point that takes a
    // tensor reads the type through it.
    return type::isNumericArrayType(type);
}

TensorFacts tensorFactsOf(const Type *type) {
    if (type == nullptr) {
        return {};
    }
    const ArrayType *array = type::asArrayType(type);
    if (array == nullptr) {
        if (isSupportedTensorScalar(type->code())) {
            // A scalar argument: rank 0, the element type it has.
            return {type->code(), StaticShape{}};
        }
        return {};
    }
    // A numeric array (`f32[2, 3]`, the spelling the alias hands out, and any numeric array a
    // program writes): the element type is the dtype and the stated extents are the shape. An
    // extent nobody wrote is unknown, and an array whose rank is open states no shape at all.
    // Every array a value can have states its element type (design principles 5.1), so there is
    // no dtype-less array to answer for.
    if (array->elemType() != nullptr && isSupportedTensorScalar(array->elemType()->code())) {
        TensorFacts facts;
        facts.dtype = array->elemType()->code();
        if (!array->anyRank()) {
            StaticShape shape;
            shape.reserve(array->rank());
            for (const ExtentForm &form : array->extents()) {
                shape.push_back(form.isConstant() ? std::optional(form.constant()) : std::nullopt);
            }
            facts.shape = std::move(shape);
        }
        return facts;
    }
    // A nested array (`f32[][]`): the innermost element type decides the dtype, the nesting depth
    // is the rank, and the extents of a written-out literal are not known.
    Type *elem   = array->elemType();
    size_t depth = 1;
    while (elem->code() == TypeCode::Array) {
        elem = static_cast<ArrayType *>(elem)->elemType();
        ++depth;
    }
    TensorFacts facts;
    if (isSupportedTensorScalar(elem->code())) {
        facts.dtype = elem->code();
    }
    facts.shape = StaticShape(depth, std::nullopt);
    return facts;
}

} // namespace camel::tensor