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
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The runtime tensor object.
 *
 * A TensorObject is a single GC allocation holding its header, its shape, and
 * a contiguous row-major element buffer. It holds no GC references, so the
 * collector can move it freely; `onMoved` re-derives the interior pointers.
 *
 * The per-element accessors (`getAsDouble`, ...) exist for printing and for
 * scalar reads. Kernels must use typed buffers through `dataAs<T>()`.
 */

#pragma once

#include "camel/core/mm/alloc/allocator.h"
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

class TensorObject : public rtdata::Object {
  public:
    TensorObject(const TensorObject &)            = delete;
    TensorObject &operator=(const TensorObject &) = delete;

    /// Allocates a tensor. `dtype` may be any numeric scalar code; it is normalized to storage.
    static TensorObject *create(
        type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator,
        bool zeroInit = false);

    size_t rank() const { return rank_; }
    uint64_t numel() const { return numel_; }
    type::TypeCode dtype() const { return dtype_; }
    size_t byteSize() const { return byteSize_; }
    const int64_t *shape() const { return shape_; }
    std::span<const int64_t> shapeSpan() const { return {shape_, rank_}; }
    Shape shapeVector() const { return Shape(shape_, shape_ + rank_); }
    int64_t dim(size_t index) const;
    const std::byte *rawData() const { return data_; }
    std::byte *rawData() { return data_; }

    template <typename T> T *dataAs() { return reinterpret_cast<T *>(data_); }
    template <typename T> const T *dataAs() const { return reinterpret_cast<const T *>(data_); }

    bool sameShape(const TensorObject *other) const;
    double getAsDouble(uint64_t flatIndex) const;
    int64_t getAsInt64(uint64_t flatIndex) const;
    bool getAsBool(uint64_t flatIndex) const;
    void setFromDouble(uint64_t flatIndex, double value);
    void setFromInt64(uint64_t flatIndex, int64_t value);
    void setFromBool(uint64_t flatIndex, bool value);

    bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override;
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;

  private:
    TensorObject(type::TypeCode dtype, uint32_t rank, uint64_t numel, uint64_t byteSize);
    void refreshPointers();
    void printRecursive(
        std::ostream &os, size_t dimIndex, uint64_t &flatIndex, const std::string &indent) const;
    std::string formatElementToString(uint64_t flatIndex) const;

    type::TypeCode dtype_;
    uint32_t rank_;
    uint64_t numel_;
    uint64_t byteSize_;
    int64_t *shape_;
    std::byte *data_;
    std::byte storage_[];
};

} // namespace camel::tensor
