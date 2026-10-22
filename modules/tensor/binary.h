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
} // namespace camel::tensor
