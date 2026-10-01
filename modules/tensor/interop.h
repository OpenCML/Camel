/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 10, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
::Array *makeShapeArray(const TensorObject *tensor, mm::IAllocator &allocator);

/// Converts a (nested) numeric array into a tensor; nesting depth becomes rank.
TensorObject *tensorFromArray(slot_t slot, type::Type *valueType, mm::IAllocator &allocator);

DLManagedTensor *tensorToDLPackCopy(const TensorObject *tensor);
TensorObject *tensorFromDLPackCopy(const DLManagedTensor *managed, mm::IAllocator &allocator);

} // namespace camel::tensor
