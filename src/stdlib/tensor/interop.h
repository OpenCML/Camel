// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Conversions between tensors and other runtime values: Camel scalars,
 * (nested) Camel arrays, shape arrays, and DLPack tensors.
 */

#pragma once

#include "camel/core/rtdata/array.h"
#include "dlpack.h"
#include "tensor.h"

namespace camel::tensor {

/// Reads a Camel numeric scalar slot of type `code` as double / int64 / bool.
double scalarToDouble(type::TypeCode code, slot_t value);
int64_t scalarToInt64(type::TypeCode code, slot_t value);
bool scalarToBool(type::TypeCode code, slot_t value);

/// Parses an int[] value as a list of integers without validation.
std::vector<int64_t> parseIntArray(const ::Array *array, const type::Type *arrayType);
/// Parses an int[] value into a shape. Negative extents are rejected.
Shape parseShapeArray(const ::Array *shapeArray, const type::Type *shapeType);
/// Builds an int[] value holding the tensor's shape.
::Array *makeShapeArray(const Array *tensor, mm::IAllocator &allocator);

/// Converts a (nested) numeric array into a tensor; nesting depth becomes rank.
Array *tensorFromArray(slot_t slot, type::Type *valueType, mm::IAllocator &allocator);

} // namespace camel::tensor
