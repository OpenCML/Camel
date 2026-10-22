// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Tensor values are arrays (core/rtdata/array.h): the element type is the dtype and the shape
 * is the array's extents, so a tensor's data is a compact block of numbers with no separate
 * tensor object. A tensor view is an Array view of the source's block; it keeps the block
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

/// Allocates a tensor. `dtype` is any supported element type and is stored exactly as written
/// (dtype.h); an unsupported code is refused.
Array *createTensor(
    type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator,
    bool zeroInit = false);

/**
 * A tensor with `source`'s elements and dtype and the given shape, starting at element
 * `elementOffset` of `source` (which must leave at least the shape's element count).
 * It shares `source`'s block instead of copying it. Only the collected heap (mm::autoSpace)
 * can hold a view; for any other allocator this copies the elements.
 */
Array *viewTensor(
    const Array *source, std::span<const int64_t> shape, mm::IAllocator &allocator,
    uint64_t elementOffset = 0);

/**
 * Refuses a tensor whose element type a kernel does not compute in, as
 * `RuntimeDiag::OperandTypeUnsupported`: the message names the operator, the argument's
 * element type and the set the kernel takes. A kernel that supports only some element types
 * calls this instead of converting the tensor (design principles 5.1: no implicit conversion
 * between numeric types; 13.1: a construct the implementation cannot handle is refused, never
 * silently done another way).
 */
[[noreturn]] void refuseTensorElementType(
    std::string_view op, const Array *tensor,
    std::initializer_list<type::TypeCode> supported);

} // namespace camel::tensor