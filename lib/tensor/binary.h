// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Bit-preserving, little-endian tensor payloads, independent of file formats.

#pragma once
#include "tensor.h"
#include <string>
#include <string_view>

namespace camel::tensor {
size_t tensorPayloadSize(type::TypeCode dtype, std::span<const int64_t> shape);
std::string tensorToLittleEndian(const Array *tensor);
Array *tensorFromLittleEndian(
    std::string_view bytes, type::TypeCode dtype, std::span<const int64_t> shape,
    mm::IAllocator &allocator);

/// The same payload for an array whose element type is pure data but not one scalar the tensor
/// module stores: each element is written as its layout's bytes, field by field, and each field
/// as little-endian bytes of its own width. The padding the layout keeps between and after the
/// fields is part of the element, so the payload is the element's own bytes and two equal values
/// have equal payloads.
std::string plainSequenceToLittleEndian(const Array *array);

/// The reverse: an array of `arrayType`'s element type and `extents` read out of that payload.
/// The byte count must be the one the layout and the shape require, and the element type must be
/// a struct or a tuple with a layout this reader walks. The array starts zeroed and each field is
/// written at its own offset, so the padding is the layout's zeros.
Array *plainSequenceFromLittleEndian(
    std::string_view bytes, const type::ArrayType *arrayType, std::span<const int64_t> extents,
    mm::IAllocator &allocator);
} // namespace camel::tensor
