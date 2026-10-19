/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Bit-preserving, little-endian tensor payloads, independent of file formats.
#pragma once
#include "tensor.h"
#include <string>
#include <string_view>

namespace camel::tensor {
std::string tensorToLittleEndian(const TensorObject *tensor);
TensorObject *tensorFromLittleEndian(
    std::string_view bytes, type::TypeCode dtype, std::span<const int64_t> shape,
    mm::IAllocator &allocator);
} // namespace camel::tensor
