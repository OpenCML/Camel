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
 * Created: Jul. 29, 2025
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tensor values are arrays (core/rtdata/array.h): the element type is the dtype and the shape
 * is the array's extents, so a tensor's data is a compact numeric buffer with no separate
 * tensor object. A tensor view is an Array view of the source's buffer; it keeps the buffer
 * alive and shares it without a copy.
 *
 * This header keeps the tensor module's spellings (dtype, shape, numel, data accessors) and the
 * constructors that map a dtype to the array's element type.
 */

#pragma once

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "dtype.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace camel::tensor {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;

using ::Array;
using Shape = std::vector<int64_t>;

/**
 * Thrown by kernels when operand shapes are incompatible (broadcasting,
 * contraction, element-count, or concatenation conflicts). Operator wrappers
 * report it as RuntimeDiag::TensorDimensionMismatch; other std::invalid_argument
 * errors (bad axis, bad dtype, ...) are reported as generic runtime errors.
 */
class ShapeError : public std::invalid_argument {
  public:
    using std::invalid_argument::invalid_argument;
};

/// Product of all extents; 1 for a scalar (rank-0) shape.
uint64_t numelOf(std::span<const int64_t> shape);

/// Allocates a tensor. `dtype` may be any numeric scalar code; it is normalized to storage.
Array *createTensor(
    type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator,
    bool zeroInit = false);

/**
 * A tensor with `source`'s elements and dtype and the given shape, starting at element
 * `elementOffset` of `source` (which must leave at least the shape's element count).
 * It shares `source`'s buffer instead of copying it. Only the collected heap (mm::autoSpace)
 * can hold a view; for any other allocator this copies the elements.
 */
Array *viewTensor(
    const Array *source, std::span<const int64_t> shape, mm::IAllocator &allocator,
    uint64_t elementOffset = 0);

} // namespace camel::tensor