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

#include <optional>
#include <span>
#include <vector>

namespace camel::tensor {

using type::ArrayType;
using type::ExtentForm;
using type::Type;
using type::TypeCode;

bool isTensorType(const Type *type) {
    // A tensor is a numeric array (plan section 5.2): the alias `Tensor` is the numeric array
    // whose element type is not fixed, and a numeric array a program writes (`f32[2, 3]`) is a
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
    if (array->isNumericUnknown()) {
        // `Tensor`: no element type is fixed. A shape argument fixes the rank and its extents;
        // a bare `Tensor` fixes neither.
        TensorFacts facts;
        if (!array->anyRank()) {
            StaticShape shape;
            shape.reserve(array->rank());
            for (const ExtentForm &form : array->extents()) {
                shape.push_back(form.isConstant() ? form.constant() : kUnknownDim);
            }
            facts.shape = std::move(shape);
        }
        return facts;
    }
    // A numeric array (`f32[2, 3]`, the alias's other spelling, and any numeric array a program
    // writes): the element type is the dtype and the stated extents are the shape. An extent
    // nobody wrote is unknown, and an array whose rank is open states no shape at all.
    if (array->elemType() != nullptr && isSupportedTensorScalar(array->elemType()->code())) {
        TensorFacts facts;
        facts.dtype = array->elemType()->code();
        if (!array->anyRank()) {
            StaticShape shape;
            shape.reserve(array->rank());
            for (const ExtentForm &form : array->extents()) {
                shape.push_back(form.isConstant() ? form.constant() : kUnknownDim);
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
    facts.shape = StaticShape(depth, kUnknownDim);
    return facts;
}

} // namespace camel::tensor