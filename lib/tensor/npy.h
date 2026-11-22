// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * NumPy `.npy` file reading and writing.
 *
 * Reading accepts format versions 1.x-3.x, C order, and the element types
 * float32, float64, int32, int64 and bool, in either byte order: the file's
 * element type is the tensor's, and nothing is converted. A caller that knows
 * the element type the program declared for the load passes it, and a file of
 * another element type is then refused (npy.cpp).
 * Writing produces version 1.0 files in the tensor's storage dtype (<f4, <i8, |b1).
 */

#pragma once

#include "tensor.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace camel::tensor {

/// The tensor a `.npy` file holds. `declaredElement`, when given, is the element type the program
/// declared for this load (`load_npy::<f32>(p)`, or the element type of the position the call
/// stands in): the file's own element type must be that one, and any other is reported as a fault
/// naming both rather than converted (design principles 5.1: no implicit conversion between
/// numeric types). A declared type no `.npy` file can hold is refused the same way.
/// Without it the file's element type is the tensor's, as it always was.
Array *loadNpy(
    const std::filesystem::path &path, mm::IAllocator &allocator,
    std::optional<type::TypeCode> declaredElement = std::nullopt);

void saveNpy(const Array *tensor, const std::filesystem::path &path);

/// Owned NPY payloads for atomic checkpoint publication. No Camel heap pointers survive IO.
std::string
encodeNpy(type::TypeCode dtype, std::span<const int64_t> shape, std::string_view payload);
/// Strict storage decoding: no float64/int32 conversion or trailing data.
Array *decodeNpyExact(std::string_view bytes, mm::IAllocator &allocator);
/// Validate the complete owned input before allocating any Camel objects.
struct NpyStorageView {
    type::TypeCode dtype;
    Shape shape;
    std::string_view payload;
    bool bigEndian;
};
NpyStorageView inspectNpyExact(std::string_view bytes);

} // namespace camel::tensor
