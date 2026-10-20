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
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * NumPy `.npy` file reading and writing.
 *
 * Reading accepts format versions 1.x-3.x, C order, and the element types
 * float32/float64 (loaded as float32), int32/int64 (loaded as int64), and
 * bool, in either byte order. Writing produces version 1.0 files in the
 * tensor's storage dtype (<f4, <i8, |b1).
 */

#pragma once

#include "tensor.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace camel::tensor {

Array *loadNpy(const std::filesystem::path &path, mm::IAllocator &allocator);

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
